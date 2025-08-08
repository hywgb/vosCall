#include <httplib.h>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>
#include "common/env.hpp"
#include "common/log.hpp"
#include <hyperswitch/routing/route.grpc.pb.h>

int main() {
  hs::init_logging(hs::get_env("LOG_LEVEL", "info"));
  std::string bind = hs::get_env("BIND", "0.0.0.0:8080");
  std::string route_addr = hs::get_env("ROUTE_SVC_ADDR", "localhost:7001");

  auto channel = grpc::CreateChannel(route_addr, grpc::InsecureChannelCredentials());
  auto route_stub = hyperswitch::routing::RouteService::NewStub(channel);

  httplib::Server svr;
  svr.Post("/internal/route/pick", [&](const httplib::Request& req, httplib::Response& res){
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
      hyperswitch::routing::PickResponse presp;
      auto st = route_stub->Pick(&ctx, preq, &presp);
      if (!st.ok()) {
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
  svr.listen(host.c_str(), port);
  return 0;
}