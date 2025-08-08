#include "common/redis.hpp"
#include <sw/redis++/redis++.h>
#include <spdlog/spdlog.h>

namespace hs {

RedisClient::RedisClient(const std::string& uri) {
  redis_ = std::make_unique<sw::redis::Redis>(uri);
  redis_->ping();
  spdlog::info("Connected to Redis: {}", uri);
}

sw::redis::Redis& RedisClient::get() { return *redis_; }

}