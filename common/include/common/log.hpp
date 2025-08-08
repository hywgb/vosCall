#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace hs {

inline void init_logging(const std::string& level = "info") {
  auto logger = spdlog::stdout_color_mt("hs");
  spdlog::set_default_logger(logger);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
  spdlog::set_level(spdlog::level::from_str(level));
}

}