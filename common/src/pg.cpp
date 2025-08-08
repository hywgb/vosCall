#include "common/pg.hpp"
#include <spdlog/spdlog.h>

namespace hs {

Pg::Pg(const std::string& conninfo) {
  connection_ = std::make_unique<pqxx::connection>(conninfo);
  if (!connection_->is_open()) {
    throw std::runtime_error("Failed to open PostgreSQL connection");
  }
  spdlog::info("Connected to PostgreSQL: {}", connection_->dbname());
}

pqxx::connection& Pg::conn() { return *connection_; }

}