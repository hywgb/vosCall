#pragma once
#include <hyperswitch/cdr/cdr.grpc.pb.h>
#include <string>
#include <thread>
#include <atomic>

namespace hs::cdr {

class CdrIngestImpl final : public hyperswitch::cdr::CdrIngest::Service {
public:
  explicit CdrIngestImpl(const std::string& ch_http_endpoint);
  ~CdrIngestImpl();
  ::grpc::Status Push(::grpc::ServerContext* ctx, const hyperswitch::cdr::CdrEvent* req,
                      hyperswitch::cdr::Ack* resp) override;
private:
  std::string ch_http_;
  std::thread flush_thread_;
  std::atomic<bool> stop_{false};
};

}