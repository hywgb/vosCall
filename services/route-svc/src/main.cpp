#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <pthread.h>
#include <thread>
#include <atomic>
#include "common/env.hpp"
#include "common/log.hpp"
#include "common/pg.hpp"
#include "common/redis.hpp"
#include "route_service_impl.hpp"

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string pg_uri = hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch");
  std::string redis_uri = hs::get_env("REDIS_URI", "tcp://localhost:6379");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7001");

  hs::Pg pg(pg_uri);
  hs::RedisClient redis(redis_uri);

  // Enable default gRPC health service and server reflection
  grpc::EnableDefaultHealthCheckService(true);
  // grpc reflections requires linking grpc++_reflection; call stays but linking must be added in CMake
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  hs::routing::RouteServiceImpl service(&pg, &redis);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  spdlog::info("route-svc listening on {}", bind);

  // Graceful shutdown on SIGINT/SIGTERM
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGINT); sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
  std::thread shutdown_thread([&server, set]() mutable {
    int sig = 0; sigwait(&set, &sig);
    spdlog::info("Received signal {}, shutting down...", sig);
    server->Shutdown();
  });

  server->Wait();
  shutdown_thread.join();
  return 0;
}