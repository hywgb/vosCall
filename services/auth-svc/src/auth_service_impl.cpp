#include "auth_service_impl.hpp"
#include <spdlog/spdlog.h>

using hyperswitch::auth::SipAuthRequest;
using hyperswitch::auth::SipAuthResponse;
using hyperswitch::auth::RiskEvalRequest;
using hyperswitch::auth::RiskEvalResponse;

namespace hs::auth {

AuthServiceImpl::AuthServiceImpl(hs::Pg* pg, hs::RedisClient* redis) : pg_(pg), redis_(redis) {}

::grpc::Status AuthServiceImpl::SipAuth(::grpc::ServerContext*, const SipAuthRequest* req, SipAuthResponse* resp) {
  try {
    pqxx::work tx(pg_->conn());
    auto r = tx.exec_params("SELECT a.account_code, t.name FROM core.trunks t JOIN core.accounts a ON a.account_id=t.account_id WHERE t.auth_mode='ip' AND (t.auth_data->>'ip')=$1 AND t.enabled=true LIMIT 1", req->src_ip());
    if (r.empty()) { resp->set_allowed(false); resp->set_reason("ip not allowed"); return ::grpc::Status::OK; }
    resp->set_allowed(true);
    resp->set_account_code(r[0][0].as<std::string>());
    resp->set_trunk_name(r[0][1].as<std::string>());
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("SipAuth error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

::grpc::Status AuthServiceImpl::RiskEval(::grpc::ServerContext*, const RiskEvalRequest* req, RiskEvalResponse* resp) {
  try {
    // simple per-second counter using HGET/HSET (approximation)
    std::string key_prefix = "quota:cps:" + req->account_code();
    auto now = std::chrono::system_clock::now();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string sec_key = key_prefix + ":" + std::to_string(sec);

    int count = 0;
    if (auto v = redis_->hget(sec_key, "count")) {
      try { count = std::stoi(*v); } catch (...) { count = 0; }
    }
    count += 1;
    redis_->hset(sec_key, "count", std::to_string(count));
    redis_->expire(sec_key, std::chrono::seconds(10));

    // naive threshold check (future: read from DB/config)
    if (count > 1000) { resp->set_allowed(false); resp->set_reason("cps too high"); return ::grpc::Status::OK; }
    resp->set_allowed(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("RiskEval error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

}