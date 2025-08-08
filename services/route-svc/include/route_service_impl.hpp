#pragma once
#include <hyperswitch/routing/route.grpc.pb.h>
#include <memory>
#include <unordered_map>
#include <pqxx/pqxx>
#include "common/pg.hpp"
#include "common/redis.hpp"

namespace hs::routing {

class RouteServiceImpl final : public hyperswitch::routing::RouteService::Service {
public:
  RouteServiceImpl(hs::Pg* pg, hs::RedisClient* redis);
  ::grpc::Status Pick(::grpc::ServerContext* ctx, const hyperswitch::routing::PickRequest* req,
                      hyperswitch::routing::PickResponse* resp) override;
private:
  hs::Pg* pg_;
  hs::RedisClient* redis_;
};

}