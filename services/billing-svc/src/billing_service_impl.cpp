#include "billing_service_impl.hpp"
#include <spdlog/spdlog.h>
#include <pqxx/pqxx>
#include <random>

using hyperswitch::billing::AuthorizeRequest;
using hyperswitch::billing::AuthorizeResponse;
using hyperswitch::billing::RateRequest;
using hyperswitch::billing::RateResponse;
using hyperswitch::billing::SettleRequest;
using hyperswitch::billing::SettleResponse;

namespace hs::billing {

static std::string gen_token(){
  static const char* al = "0123456789abcdef";
  std::random_device rd; std::mt19937_64 g(rd());
  std::uniform_int_distribution<int> d(0,15);
  std::string s(32,'0'); for (auto &c: s) c = al[d(g)]; return s;
}

BillingServiceImpl::BillingServiceImpl(hs::Pg* pg) : pg_(pg) {}

::grpc::Status BillingServiceImpl::Authorize(::grpc::ServerContext*, const AuthorizeRequest* req, AuthorizeResponse* resp){
  try {
    pqxx::work tx(pg_->conn());
    auto r = tx.exec_params("SELECT account_id, prepaid, balance, credit_limit FROM core.accounts WHERE account_code=$1", req->account_code());
    if (r.empty()) return {::grpc::StatusCode::NOT_FOUND, "account not found"};
    auto acc_id = r[0][0].as<long long>();
    bool prepaid = r[0][1].as<bool>();
    double balance = r[0][2].as<double>();
    double credit = r[0][3].as<double>();

    // 简单额度检查：预授权按预计 3 分钟做冻结预算（可改为精确估算）
    double budget = 0.0;
    if (req->expected_secs() > 0) {
      // 需要费率估计，但此处仅做最小额度占位，后续由 Rate 提供精确
      budget = 1.0; // 最小额度
    }
    bool allowed = prepaid ? (balance > budget) : (balance + credit > budget);
    resp->set_allowed(allowed);
    if (!allowed) { resp->set_reason("insufficient funds"); return ::grpc::Status::OK; }

    std::string token = gen_token();
    // 可选：写入 authorizations 表（需要迁移支持），此处仅返回 token
    resp->set_auth_token(token);
    resp->set_authorized_amount(budget);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("Authorize error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

::grpc::Status BillingServiceImpl::Rate(::grpc::ServerContext*, const RateRequest* req, RateResponse* resp){
  try {
    pqxx::work tx(pg_->conn());
    // 选择费率表：按账户默认最新生效
    auto rt = tx.exec_params("SELECT rt.rate_table_id, rt.currency FROM billing.rate_tables rt \n"
                             "JOIN core.accounts a ON a.account_id=rt.account_id \n"
                             "WHERE a.account_code=$1 AND rt.effective_from<=now() AND (rt.effective_to IS NULL OR rt.effective_to>=now()) \n"
                             "ORDER BY rt.effective_from DESC LIMIT 1", req->account_code());
    if (rt.empty()) return {::grpc::StatusCode::NOT_FOUND, "rate table not found"};
    auto rate_table_id = rt[0][0].as<long long>();

    // 最长前缀匹配费率项
    auto r = tx.exec_params("SELECT ri.price_per_min, ri.billing_step_sec, ri.min_time_sec, ri.connection_fee, ri.rounding_mode, p.prefix \n"
                            "FROM billing.rate_items ri JOIN routing.prefixes p ON p.prefix_id=ri.prefix_id \n"
                            "WHERE ri.rate_table_id=$1 AND $2 LIKE p.prefix || '%' \n"
                            "ORDER BY length(p.prefix) DESC LIMIT 1", rate_table_id, req->e164_to());
    if (r.empty()) return {::grpc::StatusCode::NOT_FOUND, "rate not found"};

    double price = r[0][0].as<double>();
    int step = r[0][1].as<int>();
    int min_time = r[0][2].as<int>();
    double conn_fee = r[0][3].as<double>();
    std::string rounding = r[0][4].as<std::string>();

    uint32_t billsec = req->billsec();
    if (billsec < static_cast<uint32_t>(min_time)) billsec = min_time;
    uint32_t units = (billsec + step - 1) / step; // ceil
    double amount = units * (price / 60.0 * step) + conn_fee;

    resp->set_unit_price(price);
    resp->set_step_sec(step);
    resp->set_min_time_sec(min_time);
    resp->set_connection_fee(conn_fee);
    resp->set_rounding_mode(rounding);
    resp->set_amount(amount);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("Rate error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

::grpc::Status BillingServiceImpl::Settle(::grpc::ServerContext*, const SettleRequest* req, SettleResponse* resp){
  // 结算：此处应依据 auth_token 与账务流水入账；为保持真实接口，本次返回成功占位（账务表需扩展）
  resp->set_success(true);
  resp->set_final_amount(0);
  return ::grpc::Status::OK;
}

}