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
#include "common/pg.hpp"
#include "common/auth.hpp"
#include <route.grpc.pb.h>
#include <pqxx/zview.hxx>

static std::atomic<uint64_t> g_route_pick_requests{0};
static std::atomic<uint64_t> g_route_pick_failures{0};

int main() {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string bind = hs::get_env("BIND", "0.0.0.0:8080");
  std::string route_addr = hs::get_env("ROUTE_SVC_ADDR", "localhost:7001");
  std::string pg_uri = hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch");
  hs::AdminAuthConfig auth_cfg{ .adminToken = hs::get_env("ADMIN_TOKEN", "") };
  int route_timeout_ms =  hs::get_env("ROUTE_TIMEOUT_MS", "800").empty() ? 800 : std::stoi(hs::get_env("ROUTE_TIMEOUT_MS", "800"));

  hs::Pg pg(pg_uri);

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

  // Jobs: list & get
  svr.Get("/api/jobs", [&](const httplib::Request& req, httplib::Response& res){
    try {
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview("SELECT job_id, job_type, status, created_at, updated_at, started_at, finished_at FROM ops.jobs ORDER BY job_id DESC LIMIT 100"));
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& row : r) {
        nlohmann::json j;
        j["job_id"] = row[0].as<long long>();
        j["job_type"] = row[1].as<std::string>();
        j["status"] = row[2].as<std::string>();
        j["created_at"] = row[3].as<std::string>();
        j["updated_at"] = row[4].as<std::string>();
        if (!row[5].is_null()) j["started_at"] = row[5].as<std::string>();
        if (!row[6].is_null()) j["finished_at"] = row[6].as<std::string>();
        arr.push_back(std::move(j));
      }
      res.set_content(arr.dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 500; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });
  svr.Get(R"(/api/jobs/(\d+))", [&](const httplib::Request& req, httplib::Response& res){
    try {
      auto id = req.matches[1].str();
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview("SELECT job_id, job_type, status, created_at, updated_at, started_at, finished_at, error FROM ops.jobs WHERE job_id=$1"), pqxx::params{id});
      if (r.empty()) { res.status = 404; res.set_content("not found","text/plain"); return; }
      nlohmann::json j;
      j["job_id"] = r[0][0].as<long long>();
      j["job_type"] = r[0][1].as<std::string>();
      j["status"] = r[0][2].as<std::string>();
      j["created_at"] = r[0][3].as<std::string>();
      j["updated_at"] = r[0][4].as<std::string>();
      if (!r[0][5].is_null()) j["started_at"] = r[0][5].as<std::string>();
      if (!r[0][6].is_null()) j["finished_at"] = r[0][6].as<std::string>();
      if (!r[0][7].is_null()) j["error"] = r[0][7].as<std::string>();
      res.set_content(j.dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 500; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // REST: /api/accounts (GET)
  svr.Get("/api/accounts", [&](const httplib::Request& req, httplib::Response& res){
    try {
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview("SELECT account_id, account_code, name, type, currency, prepaid, balance, credit_limit, status FROM core.accounts ORDER BY account_id ASC"));
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& row : r) {
        arr.push_back({
          {"account_id", row[0].as<long long>()},
          {"account_code", row[1].as<std::string>()},
          {"name", row[2].as<std::string>()},
          {"type", row[3].as<std::string>()},
          {"currency", row[4].as<std::string>()},
          {"prepaid", row[5].as<bool>()},
          {"balance", row[6].as<double>()},
          {"credit_limit", row[7].as<double>()},
          {"status", row[8].as<std::string>()}
        });
      }
      res.set_content(arr.dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 500; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // REST: /api/accounts (POST)
  svr.Post("/api/accounts", [&](const httplib::Request& req, httplib::Response& res){
    std::unordered_map<std::string,std::string> h; for (auto& kv : req.headers) h.emplace(kv.first, kv.second);
    if (!hs::is_admin_authorized(h, auth_cfg)) { res.status = 401; res.set_content("unauthorized","text/plain"); return; }
    try {
      auto j = nlohmann::json::parse(req.body);
      std::string account_code = j.at("account_code");
      std::string name = j.at("name");
      std::string type = j.value("type","customer");
      std::string currency = j.value("currency","USD");
      bool prepaid = j.value("prepaid", true);
      double credit_limit = j.value("credit_limit", 0.0);
      pqxx::work tx(pg.conn());
      tx.exec(pqxx::zview("INSERT INTO core.accounts(account_code,name,type,currency,prepaid,credit_limit) VALUES($1,$2,$3,$4,$5,$6)"),
                     pqxx::params{account_code, name, type, currency, prepaid, credit_limit});
      // audit
      tx.exec(pqxx::zview("SELECT ops.write_audit(NULL,$1,'create','account',$2,$3)"),
                     pqxx::params{nlohmann::json({{"ip", req.remote_addr}}).dump(), account_code,
                     nlohmann::json({{"name",name},{"type",type},{"currency",currency},{"prepaid",prepaid},{"credit_limit",credit_limit}}).dump()});
      tx.commit();
      res.status = 201; res.set_content(nlohmann::json({{"ok",true}}).dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 400; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // REST: /api/trunks (GET)
  svr.Get("/api/trunks", [&](const httplib::Request& req, httplib::Response& res){
    try {
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview(
        "SELECT t.trunk_id, a.account_code, t.name, t.direction, t.auth_mode, t.enabled, t.max_cps, t.max_concurrent "
        "FROM core.trunks t JOIN core.accounts a ON a.account_id=t.account_id ORDER BY t.trunk_id ASC"));
      nlohmann::json arr = nlohmann::json::array();
      for (const auto& row : r) {
        arr.push_back({
          {"trunk_id", row[0].as<long long>()},
          {"account_code", row[1].as<std::string>()},
          {"name", row[2].as<std::string>()},
          {"direction", row[3].as<std::string>()},
          {"auth_mode", row[4].as<std::string>()},
          {"enabled", row[5].as<bool>()},
          {"max_cps", row[6].as<int>()},
          {"max_concurrent", row[7].as<int>()}
        });
      }
      res.set_content(arr.dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 500; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // REST: /api/trunks (POST)
  svr.Post("/api/trunks", [&](const httplib::Request& req, httplib::Response& res){
    std::unordered_map<std::string,std::string> h; for (auto& kv : req.headers) h.emplace(kv.first, kv.second);
    if (!hs::is_admin_authorized(h, auth_cfg)) { res.status = 401; res.set_content("unauthorized","text/plain"); return; }
    try {
      auto j = nlohmann::json::parse(req.body);
      std::string account_code = j.at("account_code");
      std::string name = j.at("name");
      std::string direction = j.at("direction"); // ingress|egress
      std::string auth_mode = j.at("auth_mode"); // ip|digest
      nlohmann::json auth_data = j.value("auth_data", nlohmann::json::object());
      int max_cps = j.value("max_cps", 0);
      int max_concurrent = j.value("max_concurrent", 0);
      pqxx::work tx(pg.conn());
      auto acc = tx.exec(pqxx::zview("SELECT account_id FROM core.accounts WHERE account_code=$1"), pqxx::params{account_code});
      if (acc.empty()) throw std::runtime_error("account not found");
      long long account_id = acc[0][0].as<long long>();
      tx.exec(pqxx::zview("INSERT INTO core.trunks(account_id,name,direction,auth_mode,auth_data,max_cps,max_concurrent) VALUES($1,$2,$3,$4,$5,$6,$7)"),
                     pqxx::params{account_id, name, direction, auth_mode, auth_data.dump(), max_cps, max_concurrent});
      // audit
      tx.exec(pqxx::zview("SELECT ops.write_audit(NULL,$1,'create','trunk',$2,$3)"),
                     pqxx::params{nlohmann::json({{"ip", req.remote_addr}}).dump(), name,
                     nlohmann::json({{"account_code",account_code},{"direction",direction},{"auth_mode",auth_mode},{"max_cps",max_cps},{"max_concurrent",max_concurrent}}).dump()});
      tx.commit();
      res.status = 201; res.set_content(nlohmann::json({{"ok",true}}).dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 400; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // REST: /api/routes/simulate (proxy to gRPC RouteService.Pick)
  svr.Post("/api/routes/simulate", [&](const httplib::Request& req, httplib::Response& res){
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

  // REST: /api/rates/import (async job submit)
  svr.Post("/api/rates/import", [&](const httplib::Request& req, httplib::Response& res){
    std::unordered_map<std::string,std::string> h; for (auto& kv : req.headers) h.emplace(kv.first, kv.second);
    if (!hs::is_admin_authorized(h, auth_cfg)) { res.status = 401; res.set_content("unauthorized","text/plain"); return; }
    try {
      // 接受完整 CSV 内容（body），存入 ops.jobs 以异步处理
      nlohmann::json meta;
      if (auto it = req.headers.find("content-type"); it != req.headers.end()) meta["content_type"] = it->second;
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview("INSERT INTO ops.jobs(job_type, status, payload, payload_text) VALUES('rate_import','pending',$1,$2) RETURNING job_id"),
                              pqxx::params{meta.dump(), req.body});
      long long job_id = r[0][0].as<long long>();
      // 审计
      tx.exec(pqxx::zview("SELECT ops.write_audit(NULL,$1,'submit','job', $2, $3)"),
                     pqxx::params{nlohmann::json({{"ip", req.remote_addr}}).dump(), std::to_string(job_id), meta.dump()});
      tx.commit();
      res.status = 202; res.set_content(nlohmann::json({{"job_id", job_id}}).dump(), "application/json");
    } catch (const std::exception& ex) {
      res.status = 400; res.set_content(nlohmann::json({{"error", ex.what()}}).dump(), "application/json");
    }
  });

  // legacy internal endpoint kept for compatibility
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