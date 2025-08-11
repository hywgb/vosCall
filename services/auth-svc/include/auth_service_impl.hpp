#pragma once
#include <auth.grpc.pb.h>
#include "common/pg.hpp"
#include "common/redis.hpp"

namespace hs::auth {

class AuthServiceImpl final : public hyperswitch::auth::AuthService::Service {
public:
  AuthServiceImpl(hs::Pg* pg, hs::RedisClient* redis);
  ::grpc::Status SipAuth(::grpc::ServerContext*, const hyperswitch::auth::SipAuthRequest*, hyperswitch::auth::SipAuthResponse*) override;
  ::grpc::Status RiskEval(::grpc::ServerContext*, const hyperswitch::auth::RiskEvalRequest*, hyperswitch::auth::RiskEvalResponse*) override;
private:
  hs::Pg* pg_;
  hs::RedisClient* redis_;
};

}