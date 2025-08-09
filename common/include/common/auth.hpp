#pragma once
#include <string>
#include <unordered_map>

namespace hs {

struct AdminAuthConfig {
  std::string adminToken;
};

inline bool is_admin_authorized(const std::unordered_map<std::string,std::string>& headers,
                                const AdminAuthConfig& cfg) {
  if (cfg.adminToken.empty()) return true;
  auto it = headers.find("x-admin-token");
  if (it == headers.end()) return false;
  return it->second == cfg.adminToken;
}

}