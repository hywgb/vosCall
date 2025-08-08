#include "observe_ingest_impl.hpp"
#include <spdlog/spdlog.h>

namespace hs::observe {

ObserveIngestImpl::ObserveIngestImpl(hs::RedisClient* redis) : redis_(redis) {}

::grpc::Status ObserveIngestImpl::PushRtcp(::grpc::ServerContext*, const hyperswitch::observe::RtcpStat* stat, hyperswitch::observe::Ack* ack) {
  try {
    auto& r = redis_->get();
    // key: quality:call:<call_id>:<leg>, store latest; also aggregate per trunk/vendor requires external mapping (not in event). Here we store per-call only.
    std::string key = "quality:call:" + stat->call_id() + ":" + stat->leg();
    r.hset(key, "loss", std::to_string(stat->loss()));
    r.hset(key, "jitter_ms", std::to_string(stat->jitter_ms()));
    r.hset(key, "rtt_ms", std::to_string(stat->rtt_ms()));
    r.expire(key, std::chrono::seconds(300));
    ack->set_ok(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("PushRtcp error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

}