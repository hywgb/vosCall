#include "common/redis.hpp"
#include <hiredis/hiredis.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace hs {

static void parse_tcp_uri(const std::string& uri, std::string& host, int& port) {
  // Expect forms like tcp://host:port or redis://host:port; default port 6379
  std::string s = uri;
  auto pos = s.find("://");
  if (pos != std::string::npos) s = s.substr(pos + 3);
  auto colon = s.find(":");
  if (colon == std::string::npos) {
    host = s; port = 6379; return;
  }
  host = s.substr(0, colon);
  try { port = std::stoi(s.substr(colon + 1)); } catch (...) { port = 6379; }
}

RedisClient::RedisClient(const std::string& uri) {
  std::string host; int port = 6379;
  parse_tcp_uri(uri, host, port);
  ctx_ = redisConnect(host.c_str(), port);
  if (!ctx_ || ctx_->err) {
    std::string err = ctx_ ? ctx_->errstr : "null context";
    if (ctx_) { redisFree(ctx_); ctx_ = nullptr; }
    throw std::runtime_error("Failed to connect to Redis: " + err);
  }
  // ping
  redisReply* reply = (redisReply*)redisCommand(ctx_, "PING");
  if (!reply || reply->type == REDIS_REPLY_ERROR) {
    if (reply) freeReplyObject(reply);
    redisFree(ctx_); ctx_ = nullptr;
    throw std::runtime_error("Failed to ping Redis");
  }
  if (reply) freeReplyObject(reply);
  spdlog::info("Connected to Redis at {}:{}", host, port);
}

RedisClient::~RedisClient() {
  if (ctx_) redisFree(ctx_);
}

std::optional<std::string> RedisClient::hget(const std::string& key, const std::string& field) {
  redisReply* reply = (redisReply*)redisCommand(ctx_, "HGET %s %s", key.c_str(), field.c_str());
  if (!reply) return std::nullopt;
  std::optional<std::string> out;
  if (reply->type == REDIS_REPLY_STRING && reply->str) {
    out = std::string(reply->str, reply->len);
  }
  freeReplyObject(reply);
  return out;
}

void RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
  redisReply* reply = (redisReply*)redisCommand(ctx_, "HSET %s %s %b", key.c_str(), field.c_str(), value.data(), (size_t)value.size());
  if (!reply) throw std::runtime_error("redis HSET failed");
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err = reply->str ? reply->str : "unknown";
    freeReplyObject(reply);
    throw std::runtime_error("redis HSET error: " + err);
  }
  freeReplyObject(reply);
}

void RedisClient::expire(const std::string& key, std::chrono::seconds ttl) {
  redisReply* reply = (redisReply*)redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), (int)ttl.count());
  if (!reply) throw std::runtime_error("redis EXPIRE failed");
  if (reply->type == REDIS_REPLY_ERROR) {
    std::string err = reply->str ? reply->str : "unknown";
    freeReplyObject(reply);
    throw std::runtime_error("redis EXPIRE error: " + err);
  }
  freeReplyObject(reply);
}

}