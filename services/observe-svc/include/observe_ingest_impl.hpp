#pragma once
#include <hyperswitch/observe/observe.grpc.pb.h>
#include "common/redis.hpp"

namespace hs::observe {

class ObserveIngestImpl final : public hyperswitch::observe::ObserveIngest::Service {
public:
  explicit ObserveIngestImpl(hs::RedisClient* redis);
  ::grpc::Status PushRtcp(::grpc::ServerContext*, const hyperswitch::observe::RtcpStat*, hyperswitch::observe::Ack*) override;
private:
  hs::RedisClient* redis_;
};

}