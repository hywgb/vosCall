cmake_minimum_required(VERSION 3.25)

add_library(hs_common
  src/env.cpp
  src/log.cpp
  src/pg.cpp
  src/redis.cpp
)

target_include_directories(hs_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-libpqxx CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)

# Link
target_link_libraries(hs_common PUBLIC spdlog::spdlog fmt::fmt unofficial::libpqxx::pqxx redis++::redis++ hiredis)