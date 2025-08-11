#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <pthread.h>
#include <thread>
#include <atomic>
#include <httplib.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include "common/pg.hpp"
#include "billing_service_impl.hpp"

std::atomic<uint64_t> g_billing_rate_requests{0};
std::atomic<uint64_t> g_billing_authorize_requests{0};
std::atomic<uint64_t> g_billing_settle_requests{0};
std::atomic<uint64_t> g_billing_errors{0};

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string pg_uri = hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7003");
  int metrics_port = std::stoi(hs::get_env("METRICS_PORT", "9103"));

  hs::Pg pg(pg_uri);
  hs::billing::BillingServiceImpl svc(&pg);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);

  auto server = builder.BuildAndStart();
  spdlog::info("billing-svc listening on {}", bind);

  // HTTP metrics/health server
  httplib::Server http;
  http.Get("/healthz", [](const httplib::Request&, httplib::Response& res){ res.set_content("ok","text/plain"); });
  http.Get("/readyz", [&](const httplib::Request&, httplib::Response& res){
    bool ok = true; try { pqxx::work tx(pg.conn()); (void)tx.exec(pqxx::zview("SELECT 1")); tx.commit(); } catch (...) { ok = false; }
    res.status = ok ? 200 : 503; res.set_content(ok?"ok":"not_ready","text/plain");
  });
  http.Get("/metrics", [](const httplib::Request&, httplib::Response& res){
    std::string body;
    body += "billing_rate_requests_total " + std::to_string(g_billing_rate_requests.load()) + "\n";
    body += "billing_authorize_requests_total " + std::to_string(g_billing_authorize_requests.load()) + "\n";
    body += "billing_settle_requests_total " + std::to_string(g_billing_settle_requests.load()) + "\n";
    body += "billing_errors_total " + std::to_string(g_billing_errors.load()) + "\n";
    res.set_content(body, "text/plain; version=0.0.4");
  });
  std::thread http_thread([&]{ http.listen("0.0.0.0", metrics_port); });

  // Graceful shutdown on SIGINT/SIGTERM
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGINT); sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
  std::thread shutdown_thread([&server, &http, set]() mutable {
    int sig = 0; sigwait(&set, &sig);
    spdlog::info("Received signal {}, shutting down...", sig);
    server->Shutdown();
    http.stop();
  });

  server->Wait();
  if (http_thread.joinable()) http_thread.join();
  shutdown_thread.join();
  return 0;
}