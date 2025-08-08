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
    std::cerr << "Usage: loader <mode> <file>\n  modes: currencies|e164|fx\n";
    return 1;
  }
  std::string mode = argv[1];
  std::string file = argv[2];
  hs::Pg pg(hs::get_env("PG_URI", "postgresql://admin:admin@localhost:5432/hyperswitch"));
  pqxx::work tx(pg.conn());

  if (mode == "currencies") {
    auto rows = read_csv(file);
    pqxx::stream_to st = pqxx::stream_to::table(tx, "billing.currencies", {"code","name"});
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
  } else {
    std::cerr << "Unknown mode\n"; return 1;
  }
  return 0;
}