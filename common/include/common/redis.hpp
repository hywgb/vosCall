#pragma once
#include <memory>
#include <string>
#include <optional>
#include <chrono>

struct redisContext; // forward decl from hiredis

namespace hs {

class RedisClient {
public:
  explicit RedisClient(const std::string& uri);
  ~RedisClient();

  // Basic operations used by services
  std::optional<std::string> hget(const std::string& key, const std::string& field);
  void hset(const std::string& key, const std::string& field, const std::string& value);
  void expire(const std::string& key, std::chrono::seconds ttl);

private:
  redisContext* ctx_ {nullptr};
};

}