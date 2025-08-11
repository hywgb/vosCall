#include "auth_service_impl.hpp"
#include <spdlog/spdlog.h>
#include <pqxx/zview.hxx>

using hyperswitch::auth::SipAuthRequest;
using hyperswitch::auth::SipAuthResponse;
using hyperswitch::auth::RiskEvalRequest;
using hyperswitch::auth::RiskEvalResponse;

namespace hs::auth {

AuthServiceImpl::AuthServiceImpl(hs::Pg* pg, hs::RedisClient* redis, int cpsLimit)
  : pg_(pg), redis_(redis), cps_limit_(cpsLimit) {}

::grpc::Status AuthServiceImpl::SipAuth(::grpc::ServerContext*, const SipAuthRequest* req, SipAuthResponse* resp) {
  try {
    pqxx::work tx(pg_->conn());
    auto r = tx.exec(pqxx::zview("SELECT a.account_code, t.name FROM core.trunks t JOIN core.accounts a ON a.account_id=t.account_id WHERE t.auth_mode='ip' AND (t.auth_data->>'ip')=$1 AND t.enabled=true LIMIT 1"), pqxx::params{req->src_ip()});
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
    // sliding window per-second counter
    std::string key_prefix = "quota:cps:" + req->account_code();
    auto now = std::chrono::system_clock::now();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string sec_key = key_prefix + ":" + std::to_string(sec);

    long long count = redis_->incr(sec_key);
    if (count == 1) {
      redis_->expire(sec_key, std::chrono::seconds(10));
    }

    if (count > cps_limit_) { resp->set_allowed(false); resp->set_reason("cps too high"); return ::grpc::Status::OK; }
    resp->set_allowed(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("RiskEval error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

}