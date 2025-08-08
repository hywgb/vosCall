#include "cdr_ingest_impl.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <deque>

using hyperswitch::cdr::CdrEvent;
using hyperswitch::cdr::Ack;

namespace hs::cdr {

static std::mutex g_mu;
static std::deque<std::string> g_queue; // JSONEachRow lines
static const size_t FLUSH_THRESHOLD = 200;

CdrIngestImpl::CdrIngestImpl(const std::string& ch_http_endpoint) : ch_http_(ch_http_endpoint) {}

static void flush_batch(const std::string& ch_http) {
  if (g_queue.empty()) return;
  std::string body;
  body.reserve(g_queue.size() * 256);
  while (!g_queue.empty()) { body += g_queue.front(); body += "\n"; g_queue.pop_front(); }
  std::string sql = "INSERT INTO cdr FORMAT JSONEachRow\n" + body;
  auto r = cpr::Post(cpr::Url{ch_http}, cpr::Body{sql}, cpr::Timeout{5000});
  if (r.status_code < 200 || r.status_code >= 300) {
    spdlog::error("ClickHouse batch error {}: {}", r.status_code, r.text);
  }
}

::grpc::Status CdrIngestImpl::Push(::grpc::ServerContext* ctx, const CdrEvent* req, Ack* resp) {
  try {
    nlohmann::json row = {
      {"call_id", req->call_id()},
      {"attempt", req->attempt()},
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
    // billsec 计算（若有）
    if (!req->answer_ts().empty() && !req->end_ts().empty()) {
      row["billsec"] = 0; // 交给 CH 凭表达式计算或 ETL 后续处理（可保留 0）
    }

    std::string line = row.dump();
    {
      std::lock_guard<std::mutex> lk(g_mu);
      g_queue.push_back(line);
      if (g_queue.size() >= FLUSH_THRESHOLD) {
        // flush synchronously for now
        flush_batch(ch_http_);
      }
    }
    resp->set_ok(true);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("CDR ingest error: {}", ex.what());
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, ex.what());
  }
}

}