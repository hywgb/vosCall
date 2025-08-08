#pragma once
#include <string>
#include <cstdlib>

namespace hs {

inline std::string get_env(const char* key, const std::string& def = {}) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : def;
}

}