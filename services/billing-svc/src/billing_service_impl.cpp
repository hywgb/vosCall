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
    auto r = tx.exec_params("SELECT account_id, prepaid, balance, credit_limit, currency FROM core.accounts WHERE account_code=$1", req->account_code());
    if (r.empty()) return {::grpc::StatusCode::NOT_FOUND, "account not found"};
    auto acc_id = r[0][0].as<long long>();
    bool prepaid = r[0][1].as<bool>();
    double balance = r[0][2].as<double>();
    double credit = r[0][3].as<double>();
    std::string ccy = r[0][4].as<std::string>();

    double budget = 0.01; // 最小预授权
    bool allowed = prepaid ? (balance > budget) : (balance + credit > budget);
    resp->set_allowed(allowed);
    if (!allowed) { resp->set_reason("insufficient funds"); return ::grpc::Status::OK; }

    std::string token = gen_token();
    tx.exec_params("INSERT INTO billing.authorizations(call_id,account_id,token,amount,currency,status) VALUES($1,$2,$3,$4,$5,'open')",
                   req->call_id(), acc_id, token, budget, ccy);
    tx.commit();
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
    auto rt = tx.exec_params("SELECT rt.rate_table_id, rt.currency FROM billing.rate_tables rt \n"
                             "JOIN core.accounts a ON a.account_id=rt.account_id \n"
                             "WHERE a.account_code=$1 AND rt.effective_from<=now() AND (rt.effective_to IS NULL OR rt.effective_to>=now()) \n"
                             "ORDER BY rt.effective_from DESC LIMIT 1", req->account_code());
    if (rt.empty()) return {::grpc::StatusCode::NOT_FOUND, "rate table not found"};
    auto rate_table_id = rt[0][0].as<long long>();

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
  try {
    pqxx::work tx(pg_->conn());
    // 重新计价
    auto rt = tx.exec_params("SELECT rt.rate_table_id, a.account_id, a.prepaid FROM billing.rate_tables rt \n"
                             "JOIN core.accounts a ON a.account_id=rt.account_id \n"
                             "WHERE a.account_code=$1 AND rt.effective_from<=now() AND (rt.effective_to IS NULL OR rt.effective_to>=now()) \n"
                             "ORDER BY rt.effective_from DESC LIMIT 1", req->account_code());
    if (rt.empty()) return {::grpc::StatusCode::NOT_FOUND, "rate table not found"};
    auto rate_table_id = rt[0][0].as<long long>();
    auto account_id = rt[0][1].as<long long>();
    bool prepaid = rt[0][2].as<bool>();

    auto r = tx.exec_params("SELECT ri.price_per_min, ri.billing_step_sec, ri.min_time_sec, ri.connection_fee \n"
                            "FROM billing.rate_items ri JOIN routing.prefixes p ON p.prefix_id=ri.prefix_id \n"
                            "WHERE ri.rate_table_id=$1 AND $2 LIKE p.prefix || '%' \n"
                            "ORDER BY length(p.prefix) DESC LIMIT 1", rate_table_id, req->e164_to());
    if (r.empty()) return {::grpc::StatusCode::NOT_FOUND, "rate not found"};

    double price = r[0][0].as<double>();
    int step = r[0][1].as<int>();
    int min_time = r[0][2].as<int>();
    double conn_fee = r[0][3].as<double>();

    uint32_t billsec = req->billsec();
    if (billsec < static_cast<uint32_t>(min_time)) billsec = min_time;
    uint32_t units = (billsec + step - 1) / step; // ceil
    double amount = units * (price / 60.0 * step) + conn_fee;

    // 标记授权为 settled
    tx.exec_params("UPDATE billing.authorizations SET amount=$1, status='settled', updated_at=now() WHERE token=$2",
                   amount, req->auth_token());

    if (prepaid) {
      tx.exec_params("UPDATE core.accounts SET balance = balance - $1, updated_at=now() WHERE account_id=$2", amount, account_id);
    } else {
      // 后付：写入发票或应收表（此处简化记入发票金额）
      tx.exec_params("INSERT INTO billing.invoices(account_id, period_start, period_end, currency, amount_due, status) VALUES ($1, date_trunc('month', now()), date_trunc('month', now()) + interval '1 month' - interval '1 day', $2, $3, 'open') ON CONFLICT DO NOTHING",
                     account_id, req->currency(), amount);
    }

    tx.commit();
    resp->set_success(true);
    resp->set_final_amount(amount);
    return ::grpc::Status::OK;
  } catch (const std::exception& ex) {
    spdlog::error("Settle error: {}", ex.what());
    return {::grpc::StatusCode::INTERNAL, ex.what()};
  }
}

}