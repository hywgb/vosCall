#include <httplib.h>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include <csignal>
#include <pthread.h>
#include <thread>
#include <atomic>
#include "common/env.hpp"
#include "common/log.hpp"
#include <hyperswitch/routing/route.grpc.pb.h>

static std::atomic<uint64_t> g_route_pick_requests{0};
static std::atomic<uint64_t> g_route_pick_failures{0};

int main() {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string bind = hs::get_env("BIND", "0.0.0.0:8080");
  std::string route_addr = hs::get_env("ROUTE_SVC_ADDR", "localhost:7001");
  int route_timeout_ms =  hs::get_env("ROUTE_TIMEOUT_MS", "800").empty() ? 800 : std::stoi(hs::get_env("ROUTE_TIMEOUT_MS", "800"));

  auto channel = grpc::CreateChannel(route_addr, grpc::InsecureChannelCredentials());
  auto route_stub = hyperswitch::routing::RouteService::NewStub(channel);

  httplib::Server svr;
  svr.Get("/healthz", [](const httplib::Request&, httplib::Response& res){ res.set_content("ok", "text/plain"); });
  svr.Get("/readyz", [](const httplib::Request&, httplib::Response& res){ res.set_content("ok", "text/plain"); });
  svr.Get("/metrics", [](const httplib::Request&, httplib::Response& res){
    std::string body;
    body.reserve(256);
    body += "# HELP route_pick_requests_total Total route pick HTTP requests\n";
    body += "# TYPE route_pick_requests_total counter\n";
    body += "route_pick_requests_total{service=\"admin-api\"} " + std::to_string(g_route_pick_requests.load()) + "\n";
    body += "# HELP route_pick_failures_total Total failed route picks (gRPC non-OK)\n";
    body += "# TYPE route_pick_failures_total counter\n";
    body += "route_pick_failures_total{service=\"admin-api\"} " + std::to_string(g_route_pick_failures.load()) + "\n";
    res.set_content(body, "text/plain; version=0.0.4");
  });
  svr.Post("/internal/route/pick", [&](const httplib::Request& req, httplib::Response& res){
    g_route_pick_requests.fetch_add(1, std::memory_order_relaxed);
    try {
      auto j = nlohmann::json::parse(req.body);
      hyperswitch::routing::PickRequest preq;
      if (j.contains("call_id")) preq.set_call_id(j["call_id"].get<std::string>());
      if (j.contains("from_uri")) preq.set_from_uri(j["from_uri"].get<std::string>());
      if (j.contains("to_uri")) preq.set_to_uri(j["to_uri"].get<std::string>());
      if (j.contains("e164_from")) preq.set_e164_from(j["e164_from"].get<std::string>());
      if (j.contains("e164_to")) preq.set_e164_to(j["e164_to"].get<std::string>());
      if (j.contains("src_ip")) preq.set_src_ip(j["src_ip"].get<std::string>());
      if (j.contains("ingress_trunk")) preq.set_ingress_trunk(j["ingress_trunk"].get<std::string>());
      if (j.contains("codecs")) for (auto& c: j["codecs"]) preq.add_codecs(c.get<std::string>());

      grpc::ClientContext ctx;
      if (route_timeout_ms > 0) {
        auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(route_timeout_ms);
        ctx.set_deadline(deadline);
      }
      hyperswitch::routing::PickResponse presp;
      auto st = route_stub->Pick(&ctx, preq, &presp);
      if (!st.ok()) {
        g_route_pick_failures.fetch_add(1, std::memory_order_relaxed);
        res.status = 502;
        res.set_content(nlohmann::json({{"error", st.error_message()}}).dump(), "application/json");
        return;
      }
      nlohmann::json out;
      out["route_plan"] = presp.route_plan();
      out["policy_version"] = presp.policy_version();
      out["candidates"] = nlohmann::json::array();
      for (const auto& c : presp.candidates()) {
        out["candidates"].push_back({
          {"vendor", c.vendor()}, {"egress_trunk", c.egress_trunk()}, {"ip", c.ip()}, {"port", c.port()},
          {"priority", c.priority()}, {"weight", c.weight()}
        });
      }
      res.set_content(out.dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 400;
      res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  auto pos = bind.find(":");
  std::string host = bind.substr(0, pos);
  int port = std::stoi(bind.substr(pos + 1));
  spdlog::info("admin-api listening on {}:{}", host, port);

  // Graceful shutdown on SIGINT/SIGTERM for HTTP server
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGINT); sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
  std::thread shutdown_thread([&svr, set]() mutable {
    int sig = 0; sigwait(&set, &sig);
    spdlog::info("Received signal {}, stopping http server...", sig);
    svr.stop();
  });

  svr.listen(host.c_str(), port);
  shutdown_thread.join();
  return 0;
}