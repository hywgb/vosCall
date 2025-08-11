#include "cdr_ingest_impl.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <deque>
extern std::atomic<uint64_t> g_cdr_ingest_total;
extern std::atomic<uint64_t> g_cdr_errors;

using hyperswitch::cdr::CdrEvent;
using hyperswitch::cdr::Ack;

namespace hs::cdr {

static std::mutex g_mu;
static std::deque<std::string> g_queue; // JSONEachRow lines
static const size_t FLUSH_THRESHOLD = 200;

static void flush_batch_unlocked(const std::string& ch_http) {
  if (g_queue.empty()) return;
  std::string body;
  body.reserve(g_queue.size() * 256);
  while (!g_queue.empty()) { body += g_queue.front(); body += "\n"; g_queue.pop_front(); }
  std::string sql = "INSERT INTO cdr FORMAT JSONEachRow\n" + body;

  // Parse base URL and path (e.g., http://host:8123/?database=hyperswitch)
  std::string base = ch_http;
  std::string path = "/";
  auto pos = ch_http.find("/");
  if (pos != std::string::npos && pos > 7) { // after scheme
    // find third slash (scheme://host:port/..)
    auto p = ch_http.find('/', ch_http.find("://") + 3);
    if (p != std::string::npos) {
      base = ch_http.substr(0, p);
      path = ch_http.substr(p);
    }
  }

  httplib::Client cli(base.c_str());
  cli.set_connection_timeout(5);
  cli.set_read_timeout(5);
  auto res = cli.Post(path.c_str(), sql, "text/plain");
  if (!res || res->status < 200 || res->status >= 300) {
    int code = res ? res->status : 0;
    std::string text = res ? res->body : "no response";
    spdlog::error("ClickHouse batch error {}: {}", code, text);
    g_cdr_errors.fetch_add(1, std::memory_order_relaxed);
  }
}

CdrIngestImpl::CdrIngestImpl(const std::string& ch_http_endpoint) : ch_http_(ch_http_endpoint) {
  flush_thread_ = std::thread([this]{
    while (!stop_.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      try {
        std::lock_guard<std::mutex> lk(g_mu);
        flush_batch_unlocked(ch_http_);
      } catch (...) {}
    }
  });
}

CdrIngestImpl::~CdrIngestImpl() {
  try {
    stop_.store(true, std::memory_order_relaxed);
    if (flush_thread_.joinable()) flush_thread_.join();
    std::lock_guard<std::mutex> lk(g_mu);
    flush_batch_unlocked(ch_http_);
  } catch (...) {}
}

::grpc::Status CdrIngestImpl::Push(::grpc::ServerContext* ctx, const CdrEvent* req, Ack* resp) {
  try {
    nlohmann::json row = {
      {"call_id", req->call_id()},
      {"attempt", req->attempt()},
      {"phase", req->phase()},
      {"start_ts", req->start_ts()},
      {"answer_ts", req->answer_ts()},
      {"end_ts", req->end_ts()},
      {"billsec", 0},
      {"from_uri", req->from_uri()},
      {"to_uri", req->to_uri()},
      {"e164_from", req->e164_from()},
      {"e164_to", req->e164_to()},
      {"ingress_trunk", req->ingress_trunk()},
      {"egress_trunk", req->egress_trunk()},
      {"sip_final_code", req->sip_final_code()},
      {"sip_final_reason", req->sip_final_reason()},
      {"route_plan", req->route_plan()},
      {"vendor", req->vendor()},
      {"codec_in", req->codec_in()},
      {"codec_out", req->codec_out()},
      {"transcoded", req->transcoded()},
      {"pdd_ms", req->pdd_ms()},
      {"acd_seconds", req->acd_seconds()},
      {"bytes_tx", req->bytes_tx()},
      {"bytes_rx", req->bytes_rx()},
      {"node", req->node()}
    };
    if (!req->answer_ts().empty() && !req->end_ts().empty()) {
      row["billsec"] = 0;
    }

    std::string line = row.dump();
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_queue.push_back(line);
      if (g_queue.size() >= FLUSH_THRESHOLD) {
        flush_batch_unlocked(ch_http_);
      }
    }
    g_cdr_ingest_total.fetch_add(1, std::memory_order_relaxed);
    resp->set_ok(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("CDR ingest error: {}", ex.what());
    g_cdr_errors.fetch_add(1, std::memory_order_relaxed);
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, ex.what());
  }
}

}