#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include "cdr_ingest_impl.hpp"

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string ch_http = hs::get_env("CH_HTTP", "http://localhost:8123/?database=hyperswitch");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7002");

  hs::cdr::CdrIngestImpl svc(ch_http);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);
  auto server = builder.BuildAndStart();
  spdlog::info("cdr-svc listening on {}", bind);
  server->Wait();
  return 0;
}