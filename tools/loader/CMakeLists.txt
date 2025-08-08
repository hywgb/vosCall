cmake_minimum_required(VERSION 3.25)

add_executable(loader
  src/main.cpp
)

target_include_directories(loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(loader PRIVATE hs_common)