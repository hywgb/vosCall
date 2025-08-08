#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include "common/redis.hpp"
#include "observe_ingest_impl.hpp"

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string redis_uri = hs::get_env("REDIS_URI", "tcp://localhost:6379");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7005");

  hs::RedisClient redis(redis_uri);
  hs::observe::ObserveIngestImpl svc(&redis);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);
  auto server = builder.BuildAndStart();
  spdlog::info("observe-svc listening on {}", bind);
  server->Wait();
  return 0;
}