#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <pqxx/pqxx>
#include "common/env.hpp"
#include "common/pg.hpp"

static std::vector<std::vector<std::string>> read_csv(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<std::vector<std::string>> rows;
  std::string line; bool header_skipped=false;
  while (std::getline(in, line)) {
    if (!header_skipped) { header_skipped=true; continue; }
    std::vector<std::string> cols; std::stringstream ss(line); std::string cell;
    while (std::getline(ss, cell, ',')) cols.push_back(cell);
    rows.push_back(std::move(cols));
  }
  return rows;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: loader <mode> <file>\n  modes: currencies|e164|fx|rate_import_job\n";
    return 1;
  }
  std::string mode = argv[1];
  std::string file = argv[2];
  hs::Pg pg(hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch"));
  pqxx::work tx(pg.conn());

  if (mode == "currencies") {
    auto rows = read_csv(file);
    pqxx::stream_to st = pqxx::stream_to::table(tx, {"billing","currencies"}, {"code","name"});
    for (auto& r : rows) {
      if (r.size() < 2) continue;
      st << std::make_tuple(r[0], r[1]);
    }
    st.complete();
    tx.commit();
    std::cout << "Loaded currencies: " << rows.size() << "\n";
  } else if (mode == "e164") {
    auto rows = read_csv(file);
    // load destinations
    for (auto& r : rows) {
      if (r.size() < 6) continue;
      tx.exec_params("INSERT INTO routing.destinations(country_code,country_name,iso_numeric,calling_code,area_name) VALUES($1,$2,COALESCE(NULLIF($3,''),'000'),$4,NULLIF($5,'')) ON CONFLICT DO NOTHING",
                     r[0], r[1], r[2], r[3], r[4]);
    }
    // load prefixes
    for (auto& r : rows) {
      if (r.size() < 6 || r[5].empty()) continue;
      auto d = tx.exec_params("SELECT dest_id FROM routing.destinations WHERE country_code=$1 AND calling_code=$2 LIMIT 1", r[0], r[3]);
      if (!d.empty()) {
        auto dest_id = d[0][0].as<long long>();
        tx.exec_params("INSERT INTO routing.prefixes(dest_id,prefix,description) VALUES($1,$2,$3) ON CONFLICT DO NOTHING",
                       dest_id, r[5], (r[4].empty()?r[1]:r[4]));
      }
    }
    tx.commit();
    std::cout << "Loaded E.164 rows: " << rows.size() << "\n";
  } else if (mode == "fx") {
    auto rows = read_csv(file);
    // ECB CSV columns vary; for simplicity expect transformed to two columns (date,currency:rate) not provided here; fallback to psql path
    std::cerr << "fx loader: please use psql load_fx.sql for now\n";
    return 2;
  } else if (mode == "rate_import_job") {
    // file is job_id
    long long job_id = std::stoll(file);
    try {
      // mark running
      tx.exec_params("UPDATE ops.jobs SET status='running', started_at=now() WHERE job_id=$1 AND status='pending'", job_id);
      auto j = tx.exec_params("SELECT payload_text FROM ops.jobs WHERE job_id=$1", job_id);
      if (j.empty()) throw std::runtime_error("job not found");
      std::string csv = j[0][0].as<std::string>();
      // parse csv from memory
      std::stringstream in(csv);
      std::string line; bool header_skipped=false;
      struct Row { std::string account_code; std::string prefix; double price_per_min; int step; int min_time; double conn_fee; std::string currency; std::string table_name; };
      std::vector<Row> rows;
      while (std::getline(in, line)) {
        if (!header_skipped) { header_skipped=true; continue; }
        std::stringstream ss(line); std::string cell; std::vector<std::string> cols;
        while (std::getline(ss, cell, ',')) cols.push_back(cell);
        if (cols.size() < 8) continue;
        rows.push_back({cols[0], cols[1], std::stod(cols[2]), std::stoi(cols[3]), std::stoi(cols[4]), std::stod(cols[5]), cols[6], cols[7]});
      }
      // for each account_code + table_name, ensure rate table exists (effective_from=now())
      for (const auto& r : rows) {
        auto acc = tx.exec_params("SELECT account_id FROM core.accounts WHERE account_code=$1", r.account_code);
        if (acc.empty()) throw std::runtime_error("account not found: "+r.account_code);
        long long account_id = acc[0][0].as<long long>();
        auto rt = tx.exec_params("INSERT INTO billing.rate_tables(account_id,name,currency,effective_from) VALUES($1,$2,$3,now()) ON CONFLICT DO NOTHING RETURNING rate_table_id",
                                 account_id, r.table_name, r.currency);
        long long rate_table_id;
        if (rt.empty()) {
          auto q = tx.exec_params("SELECT rate_table_id FROM billing.rate_tables WHERE account_id=$1 AND name=$2 ORDER BY effective_from DESC LIMIT 1",
                                  account_id, r.table_name);
          rate_table_id = q[0][0].as<long long>();
        } else {
          rate_table_id = rt[0][0].as<long long>();
        }
        auto p = tx.exec_params("SELECT prefix_id FROM routing.prefixes WHERE prefix=$1 LIMIT 1", r.prefix);
        if (p.empty()) throw std::runtime_error("prefix not found: "+r.prefix);
        long long prefix_id = p[0][0].as<long long>();
        tx.exec_params("INSERT INTO billing.rate_items(rate_table_id,prefix_id,price_per_min,billing_step_sec,min_time_sec,connection_fee) VALUES($1,$2,$3,$4,$5,$6) ON CONFLICT DO NOTHING",
                       rate_table_id, prefix_id, r.price_per_min, r.step, r.min_time, r.conn_fee);
      }
      tx.exec_params("UPDATE ops.jobs SET status='done', finished_at=now() WHERE job_id=$1", job_id);
      tx.commit();
      std::cout << "job " << job_id << " done, rows: " << rows.size() << "\n";
    } catch (const std::exception& ex) {
      tx.exec_params("UPDATE ops.jobs SET status='failed', error=$2, finished_at=now() WHERE job_id=$1", job_id, ex.what());
      tx.commit();
      std::cerr << "job failed: " << ex.what() << "\n";
      return 2;
    }
  } else {
    std::cerr << "Unknown mode\n"; return 1;
  }
  return 0;
}