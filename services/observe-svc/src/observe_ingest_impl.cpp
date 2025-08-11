#include "observe_ingest_impl.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cmath>

namespace hs::observe {

ObserveIngestImpl::ObserveIngestImpl(hs::RedisClient* redis) : redis_(redis) {}

::grpc::Status ObserveIngestImpl::PushRtcp(::grpc::ServerContext*, const hyperswitch::observe::RtcpStat* stat, hyperswitch::observe::Ack* ack) {
  try {
    auto& r = *redis_;
    // key: quality:call:<call_id>:<leg>, store latest and keep short TTL
    std::string call_key = std::string("quality:call:") + stat->call_id() + ":" + stat->leg();
    r.hset(call_key, "loss", std::to_string(stat->loss()));
    r.hset(call_key, "jitter_ms", std::to_string(stat->jitter_ms()));
    r.hset(call_key, "rtt_ms", std::to_string(stat->rtt_ms()));
    r.expire(call_key, std::chrono::seconds(300));

    // lookup call -> trunk/vendor mapping
    std::string map_key = std::string("quality:callmap:") + stat->call_id();
    auto trunk = r.hget(map_key, "trunk");
    if (trunk && !trunk->empty()) {
      // compute simple penalty: 0.0..1.0 from loss and jitter heuristics
      double loss = std::clamp(stat->loss(), 0.0, 1.0);
      double jitter = std::max(0.0, stat->jitter_ms());
      double penalty = 1.0;
      // example heuristic: degrade weight by loss and jitter factor
      penalty = std::max(0.1, 1.0 - loss - std::min(jitter / 200.0, 0.5));
      std::string trunk_key = std::string("quality:trunk:") + *trunk;
      r.hset(trunk_key, "penalty", std::to_string(penalty));
      r.expire(trunk_key, std::chrono::seconds(600));
    }

    ack->set_ok(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("PushRtcp error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

}