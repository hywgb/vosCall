#pragma once
#include <route.grpc.pb.h>
#include <memory>
#include <unordered_map>
#include <atomic>
#include <pqxx/pqxx>
#include "common/pg.hpp"
#include "common/redis.hpp"

extern std::atomic<uint64_t> g_route_picks;
extern std::atomic<uint64_t> g_route_pick_errors;

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