#pragma once
#include <sw/redis++/redis++.h>
#include <memory>
#include <string>

namespace hs {

class RedisClient {
public:
  explicit RedisClient(const std::string& uri);
  sw::redis::Redis& get();
private:
  std::unique_ptr<sw::redis::Redis> redis_;
};

}