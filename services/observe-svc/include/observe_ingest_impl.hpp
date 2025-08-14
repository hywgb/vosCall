#pragma once
#include <observe.grpc.pb.h>
#include <atomic>
#include "common/redis.hpp"

extern std::atomic<uint64_t> g_rtcp_ingest_total;
extern std::atomic<uint64_t> g_observe_errors;

namespace hs::observe {

class ObserveIngestImpl final : public hyperswitch::observe::ObserveIngest::Service {
public:
  explicit ObserveIngestImpl(hs::RedisClient* redis);
  ::grpc::Status PushRtcp(::grpc::ServerContext*, const hyperswitch::observe::RtcpStat*, hyperswitch::observe::Ack*) override;
private:
  hs::RedisClient* redis_;
};

}