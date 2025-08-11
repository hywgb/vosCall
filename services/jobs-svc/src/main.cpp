#include <spdlog/spdlog.h>
#include <thread>
#include <chrono>
#include <pqxx/pqxx>
#include <pqxx/zview.hxx>
#include <atomic>
#include <httplib.h>
#include <csignal>
#include <pthread.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include "common/pg.hpp"

static std::atomic<uint64_t> g_jobs_processed{0};
static std::atomic<uint64_t> g_jobs_failed{0};
static std::atomic<bool> g_stop{false};

struct RateRow {
  std::string account_code; std::string prefix; double price_per_min; int step; int min_time; double conn_fee; std::string currency; std::string table_name;
};

static std::vector<RateRow> parse_csv(const std::string& csv) {
  std::vector<RateRow> rows;
  std::stringstream in(csv);
  std::string line; bool header_skipped=false;
  while (std::getline(in, line)) {
    if (!header_skipped) { header_skipped=true; continue; }
    std::stringstream ss(line); std::string cell; std::vector<std::string> cols;
    while (std::getline(ss, cell, ',')) cols.push_back(cell);
    if (cols.size() < 8) continue;
    rows.push_back({cols[0], cols[1], std::stod(cols[2]), std::stoi(cols[3]), std::stoi(cols[4]), std::stod(cols[5]), cols[6], cols[7]});
  }
  return rows;
}

static void process_rate_import(pqxx::work& tx, long long job_id, const std::string& csv) {
  auto rows = parse_csv(csv);
  for (const auto& r : rows) {
    auto acc = tx.exec(pqxx::zview("SELECT account_id FROM core.accounts WHERE account_code=$1"), pqxx::params{r.account_code});
    if (acc.empty()) throw std::runtime_error("account not found: "+r.account_code);
    long long account_id = acc[0][0].as<long long>();
    auto rt = tx.exec(pqxx::zview("INSERT INTO billing.rate_tables(account_id,name,currency,effective_from) VALUES($1,$2,$3,now()) ON CONFLICT DO NOTHING RETURNING rate_table_id"),
                             pqxx::params{account_id, r.table_name, r.currency});
    long long rate_table_id;
    if (rt.empty()) {
      auto q = tx.exec(pqxx::zview("SELECT rate_table_id FROM billing.rate_tables WHERE account_id=$1 AND name=$2 ORDER BY effective_from DESC LIMIT 1"),
                              pqxx::params{account_id, r.table_name});
      rate_table_id = q[0][0].as<long long>();
    } else {
      rate_table_id = rt[0][0].as<long long>();
    }
    auto p = tx.exec(pqxx::zview("SELECT prefix_id FROM routing.prefixes WHERE prefix=$1 LIMIT 1"), pqxx::params{r.prefix});
    if (p.empty()) throw std::runtime_error("prefix not found: "+r.prefix);
    long long prefix_id = p[0][0].as<long long>();
    tx.exec(pqxx::zview("INSERT INTO billing.rate_items(rate_table_id,prefix_id,price_per_min,billing_step_sec,min_time_sec,connection_fee) VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING"),
                   pqxx::params{rate_table_id, prefix_id, r.price_per_min, r.step, r.min_time, r.conn_fee});
  }
}

int main(int argc, char** argv) {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string pg_uri = hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch");
  int interval_ms = std::stoi(hs::get_env("JOB_INTERVAL_MS", "2000"));
  int metrics_port = std::stoi(hs::get_env("METRICS_PORT", "9101"));

  hs::Pg pg(pg_uri);
  spdlog::info("jobs-svc started");

  // metrics server
  httplib::Server metrics;
  metrics.Get("/healthz", [](const httplib::Request&, httplib::Response& res){ res.set_content("ok", "text/plain"); });
  metrics.Get("/readyz", [&](const httplib::Request&, httplib::Response& res){
    bool ok = true;
    try { pqxx::work tx(pg.conn()); (void)tx.exec(pqxx::zview("SELECT 1")); tx.commit(); } catch (...) { ok = false; }
    res.status = ok ? 200 : 503; res.set_content(ok?"ok":"not_ready","text/plain");
  });
  metrics.Get("/metrics", [&](const httplib::Request&, httplib::Response& res){
    std::string body;
    body += "jobs_processed_total " + std::to_string(g_jobs_processed.load()) + "\n";
    body += "jobs_failed_total " + std::to_string(g_jobs_failed.load()) + "\n";
    res.set_content(body, "text/plain; version=0.0.4");
  });
  std::thread metrics_thread([&]{ metrics.listen("0.0.0.0", metrics_port); });

  // graceful shutdown (SIGINT/SIGTERM)
  sigset_t set; sigemptyset(&set); sigaddset(&set, SIGINT); sigaddset(&set, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &set, nullptr);
  std::thread shutdown_thread([&]{
    int sig = 0; sigwait(&set, &sig);
    spdlog::info("Received signal {}, stopping jobs loop...", sig);
    g_stop.store(true, std::memory_order_relaxed);
    metrics.stop();
  });

  while (!g_stop.load(std::memory_order_relaxed)) {
    try {
      pqxx::work tx(pg.conn());
      auto r = tx.exec(pqxx::zview("SELECT job_id, job_type, payload_text FROM ops.jobs WHERE status='pending' ORDER BY created_at ASC LIMIT 1 FOR UPDATE SKIP LOCKED"));
      if (!r.empty()) {
        long long job_id = r[0][0].as<long long>();
        std::string type = r[0][1].as<std::string>();
        std::string text = r[0][2].is_null() ? std::string() : r[0][2].as<std::string>();
        tx.exec(pqxx::zview("UPDATE ops.jobs SET status='running', started_at=now() WHERE job_id=$1"), pqxx::params{job_id});
        if (type == "rate_import") {
          process_rate_import(tx, job_id, text);
        } else {
          throw std::runtime_error("unknown job type: "+type);
        }
        tx.exec(pqxx::zview("UPDATE ops.jobs SET status='done', finished_at=now() WHERE job_id=$1"), pqxx::params{job_id});
        tx.commit();
        g_jobs_processed.fetch_add(1, std::memory_order_relaxed);
        spdlog::info("job {} done", job_id);
      } else {
        tx.commit();
      }
    } catch (const std::exception& ex) {
      g_jobs_failed.fetch_add(1, std::memory_order_relaxed);
      spdlog::error("jobs loop error: {}", ex.what());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
  }
  if (metrics_thread.joinable()) metrics_thread.join();
  if (shutdown_thread.joinable()) shutdown_thread.join();
  return 0;
}