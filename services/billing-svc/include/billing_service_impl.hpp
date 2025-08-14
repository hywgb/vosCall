#pragma once
#include <billing.grpc.pb.h>
#include "common/pg.hpp"

namespace hs::billing {

class BillingServiceImpl final : public hyperswitch::billing::BillingService::Service {
public:
  explicit BillingServiceImpl(hs::Pg* pg);
  ::grpc::Status Authorize(::grpc::ServerContext*, const hyperswitch::billing::AuthorizeRequest*, hyperswitch::billing::AuthorizeResponse*) override;
  ::grpc::Status Rate(::grpc::ServerContext*, const hyperswitch::billing::RateRequest*, hyperswitch::billing::RateResponse*) override;
  ::grpc::Status Settle(::grpc::ServerContext*, const hyperswitch::billing::SettleRequest*, hyperswitch::billing::SettleResponse*) override;
private:
  hs::Pg* pg_;
};

}