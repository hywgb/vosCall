#pragma once
#include <hyperswitch/cdr/cdr.grpc.pb.h>
#include <string>

namespace hs::cdr {

class CdrIngestImpl final : public hyperswitch::cdr::CdrIngest::Service {
public:
  explicit CdrIngestImpl(const std::string& ch_http_endpoint);
  ::grpc::Status Push(::grpc::ServerContext* ctx, const hyperswitch::cdr::CdrEvent* req,
                      hyperswitch::cdr::Ack* resp) override;
private:
  std::string ch_http_;
};

}