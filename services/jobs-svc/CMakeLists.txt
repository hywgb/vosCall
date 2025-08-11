cmake_minimum_required(VERSION 3.25)

add_executable(jobs-svc
  src/main.cpp
)

target_include_directories(jobs-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(spdlog CONFIG REQUIRED)

target_link_libraries(jobs-svc PRIVATE hs_common spdlog::spdlog)