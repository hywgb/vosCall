#include "route_service_impl.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

using hyperswitch::routing::PickRequest;
using hyperswitch::routing::PickResponse;
using hyperswitch::routing::Candidate;

namespace hs::routing {

RouteServiceImpl::RouteServiceImpl(hs::Pg* pg, hs::RedisClient* redis) : pg_(pg), redis_(redis) {}

::grpc::Status RouteServiceImpl::Pick(::grpc::ServerContext* ctx, const PickRequest* req, PickResponse* resp) {
  try {
    pqxx::work txn(pg_->conn());

    // 1) 解析入口中继 -> 账号 -> 路由计划（选取最近创建作为默认）
    auto r1 = txn.exec_params(
                "SELECT rp.plan_id, rp.name FROM core.trunks t "
                "JOIN core.accounts a ON a.account_id=t.account_id "
                "JOIN routing.route_plans rp ON rp.account_id=a.account_id "
                "WHERE t.name=$1 ORDER BY rp.plan_id DESC LIMIT 1", req->ingress_trunk());

    if (r1.empty()) {
      spdlog::warn("No route plan for trunk {}", req->ingress_trunk());
      return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "route plan not found");
    }
    auto plan_id = r1[0][0].as<long long>();
    auto plan_name = r1[0][1].as<std::string>();

    // 2) 最长前缀匹配 + 供应商候选
    auto to = req->e164_to().empty() ? req->to_uri() : req->e164_to();
    auto r2 = txn.exec_params(
      "WITH cand AS (\n"
      "  SELECT p.prefix, pe.priority, pe.weight, v.name AS vendor, t.name AS trunk, t.auth_data->>'host' AS ip, \n"
      "         COALESCE((t.auth_data->>'port')::int,5060) AS port\n"
      "  FROM routing.prefixes p\n"
      "  JOIN routing.plan_entries pe ON pe.prefix_id=p.prefix_id AND pe.plan_id=$1\n"
      "  JOIN routing.vendors v ON v.vendor_id=pe.vendor_id\n"
      "  JOIN core.trunks t ON t.trunk_id=v.trunk_id\n"
      "  WHERE $2 LIKE p.prefix || '%'\n"
      ")\n"
      "SELECT prefix, priority, weight, vendor, trunk, ip, port FROM cand\n"
      " ORDER BY length(prefix) DESC, priority ASC, weight DESC LIMIT 16;",
      plan_id, to);

    if (r2.empty()) {
      return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "no route candidates");
    }

    for (const auto& row : r2) {
      Candidate* c = resp->add_candidates();
      c->set_vendor(row[3].as<std::string>());
      c->set_egress_trunk(row[4].as<std::string>());
      c->set_ip(row[5].as<std::string>());
      c->set_port(row[6].as<int>());
      c->set_priority(row[1].as<int>());
      c->set_weight(row[2].as<int>());
    }
    resp->set_route_plan(plan_name);
    resp->set_policy_version("v1");
    txn.commit();
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("Route Pick error: {}", ex.what());
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, ex.what());
  }
}

}