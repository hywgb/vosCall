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
#include "cdr_ingest_impl.hpp"

std::atomic<uint64_t> g_cdr_ingest_total{0};
std::atomic<uint64_t> g_cdr_errors{0};

static void parse_base_and_path(const std::string& url, std::string& base, std::string& path) {
  base = url; path = "/";
  auto p = url.find("/", url.find("://") + 3);
  if (p != std::string::npos) { base = url.substr(0, p); path = url.substr(p); }
}

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string ch_http = hs::get_env("CH_HTTP", "http://localhost:8123/?database=hyperswitch");
  std::string bind = hs::get_env("BIND", "0.0.0.0:7002");
  int metrics_port = std::stoi(hs::get_env("METRICS_PORT", "9106"));

  hs::cdr::CdrIngestImpl svc(ch_http);

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  grpc::ServerBuilder builder;
  builder.AddListeningPort(bind, grpc::InsecureServerCredentials());
  builder.RegisterService(&svc);
  auto server = builder.BuildAndStart();
  spdlog::info("cdr-svc listening on {}", bind);

  // HTTP metrics/health server
  httplib::Server http;
  http.Get("/healthz", [](const httplib::Request&, httplib::Response& res){ res.set_content("ok","text/plain"); });
  http.Get("/readyz", [&](const httplib::Request&, httplib::Response& res){
    bool ok = true;
    try {
      std::string base, path; parse_base_and_path(ch_http, base, path);
      httplib::Client cli(base.c_str());
      cli.set_connection_timeout(2);
      cli.set_read_timeout(2);
      auto res2 = cli.Get("/?query=SELECT%201");
      if (!res2 || res2->status != 200) ok = false;
    } catch (...) { ok = false; }
    res.status = ok ? 200 : 503; res.set_content(ok?"ok":"not_ready","text/plain");
  });
  http.Get("/metrics", [](const httplib::Request&, httplib::Response& res){
    std::string body;
    body += "cdr_ingest_total " + std::to_string(g_cdr_ingest_total.load()) + "\n";
    body += "cdr_errors_total " + std::to_string(g_cdr_errors.load()) + "\n";
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