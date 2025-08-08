#include "cdr_ingest_impl.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using hyperswitch::cdr::CdrEvent;
using hyperswitch::cdr::Ack;

namespace hs::cdr {

CdrIngestImpl::CdrIngestImpl(const std::string& ch_http_endpoint) : ch_http_(ch_http_endpoint) {}

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
    if (!req->answer_ts().empty() && !req->end_ts().empty()) {
      // compute billsec on server side optional; let CH compute later
    }

    std::string body = row.dump();
    // Use JSONEachRow insert
    std::string sql = "INSERT INTO cdr FORMAT JSONEachRow\n" + body + "\n";
    auto r = cpr::Post(cpr::Url{ch_http_}, cpr::Body{sql}, cpr::Timeout{5000});
    if (r.status_code >= 200 && r.status_code < 300) {
      resp->set_ok(true);
      return ::grpc::Status::OK;
    } else {
      spdlog::error("ClickHouse HTTP error {}: {}", r.status_code, r.text);
      return ::grpc::Status(::grpc::StatusCode::INTERNAL, "clickhouse error");
    }
  } catch (const std::exception& ex) {
    spdlog::error("CDR ingest error: {}", ex.what());
    return ::grpc::Status(::grpc::StatusCode::INTERNAL, ex.what());
  }
}

}