#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include "common/pg.hpp"
#include "billing_service_impl.hpp"

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string pg_uri = hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7003");

  hs::Pg pg(pg_uri);
  hs::billing::BillingServiceImpl svc(&pg);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);

  auto server = builder.BuildAndStart();
  spdlog::info("billing-svc listening on {}", bind);
  server->Wait();
  return 0;
}