#include "route_service_impl.hpp"
#include <spdlog/spdlog.h>
#include <sstream>
#include <pqxx/zview.hxx>

using hyperswitch::routing::PickRequest;
using hyperswitch::routing::PickResponse;
using hyperswitch::routing::Candidate;

namespace hs::routing {

RouteServiceImpl::RouteServiceImpl(hs::Pg* pg, hs::RedisClient* redis) : pg_(pg), redis_(redis) {}

::grpc::Status RouteServiceImpl::Pick(::grpc::ServerContext* ctx, const PickRequest* req, PickResponse* resp) {
  try {
    g_route_picks.fetch_add(1, std::memory_order_relaxed);
    pqxx::work txn(pg_->conn());

    auto r1 = txn.exec(pqxx::zview(
                "SELECT a.account_id, rp.plan_id, rp.name FROM core.trunks t "
                "JOIN core.accounts a ON a.account_id=t.account_id "
                "JOIN routing.route_plans rp ON rp.account_id=a.account_id "
                "WHERE t.name=$1 ORDER BY rp.plan_id DESC LIMIT 1"), pqxx::params{req->ingress_trunk()});

    if (r1.empty()) {
      spdlog::warn("No route plan for trunk {}", req->ingress_trunk());
      g_route_pick_errors.fetch_add(1, std::memory_order_relaxed);
      return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "route plan not found");
    }
    auto account_id = r1[0][0].as<long long>();
    auto plan_id = r1[0][1].as<long long>();
    auto plan_name = r1[0][2].as<std::string>();

    auto to = req->e164_to().empty() ? req->to_uri() : req->e164_to();

    // 黑名单检查（最长前缀存在则拒绝）
    auto bl = txn.exec(pqxx::zview(
      "SELECT 1 FROM security.blacklist_destinations b \n"
      "JOIN routing.prefixes p ON p.prefix_id=b.prefix_id \n"
      "WHERE (b.account_id IS NULL OR b.account_id=$1) AND $2 LIKE p.prefix || '%' AND (b.expire_at IS NULL OR b.expire_at>now()) \n"
      "ORDER BY length(p.prefix) DESC LIMIT 1"), pqxx::params{account_id, to});
    if (!bl.empty()) {
      g_route_pick_errors.fetch_add(1, std::memory_order_relaxed);
      return ::grpc::Status(::grpc::StatusCode::PERMISSION_DENIED, "destination blacklisted");
    }

    auto r2 = txn.exec(pqxx::zview(
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
      " ORDER BY length(prefix) DESC, priority ASC, weight DESC LIMIT 16;"),
      pqxx::params{plan_id, to});

    if (r2.empty()) {
      g_route_pick_errors.fetch_add(1, std::memory_order_relaxed);
      return ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "no route candidates");
    }

    // 质量衰减：从 Redis 查询 vendor/trunk penalty，乘到 weight
    for (const auto& row : r2) {
      double penalty = 1.0;
      try {
        auto key = std::string("quality:trunk:") + row[4].c_str();
        auto v = redis_->hget(key, "penalty");
        if (v) penalty = std::stod(*v);
      } catch (...) {}
      int base_weight = row[2].as<int>();
      int scaled_weight = std::max(1, static_cast<int>(base_weight * penalty));

      Candidate* c = resp->add_candidates();
      c->set_vendor(row[3].as<std::string>());
      c->set_egress_trunk(row[4].as<std::string>());
      c->set_ip(row[5].as<std::string>());
      c->set_port(row[6].as<int>());
      c->set_priority(row[1].as<int>());
      c->set_weight(scaled_weight);
      // optional capacity hints: read from table plan_entries
      try {
        auto caps = txn.exec(pqxx::zview("SELECT max_cps, max_concurrent FROM routing.plan_entries pe JOIN routing.prefixes p ON p.prefix_id=pe.prefix_id WHERE pe.plan_id=$1 AND $2 LIKE p.prefix || '%' ORDER BY length(p.prefix) DESC LIMIT 1"), pqxx::params{plan_id, to});
        if (!caps.empty()) {
          if (!caps[0][0].is_null()) c->set_max_cps(caps[0][0].as<int>());
          if (!caps[0][1].is_null()) c->set_max_concurrent(caps[0][1].as<int>());
        }
      } catch (...) {}

    }

    // 记录 call_id -> (trunk,vendor) 供 observe-svc 聚合
    if (!req->call_id().empty() && resp->candidates_size() > 0) {
      const auto& first = resp->candidates(0);
      try {
        std::string map_key = std::string("quality:callmap:") + req->call_id();
        redis_->hset(map_key, "trunk", first.egress_trunk());
        redis_->hset(map_key, "vendor", first.vendor());
        redis_->expire(map_key, std::chrono::seconds(600));
      } catch (...) {}
    }

    resp->set_route_plan(plan_name);
    resp->set_policy_version("v1");
    txn.commit();
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("Route Pick error: {}", ex.what());
    g_route_pick_errors.fetch_add(1, std::memory_order_relaxed);
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, ex.what());
  }
}

}