#pragma once
#include <pqxx/pqxx>
#include <memory>
#include <string>

namespace hs {

class Pg {
public:
  explicit Pg(const std::string& conninfo);
  pqxx::connection& conn();
private:
  std::unique_ptr<pqxx::connection> connection_;
};

}