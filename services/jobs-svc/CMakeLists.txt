cmake_minimum_required(VERSION 3.25)

add_executable(jobs-svc
  src/main.cpp
)

target_include_directories(jobs-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Find httplib via config or pkg-config
find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(HTTPLIB REQUIRED cpp-httplib)
    set(HTTPLIB_LIBS ${HTTPLIB_LIBRARIES})
  endif()
endif()

# spdlog header-only; link to hs_common for Pg and utilities
if(TARGET httplib::httplib)
  target_link_libraries(jobs-svc PRIVATE hs_common httplib::httplib)
elseif(DEFINED HTTPLIB_LIBS)
  target_link_libraries(jobs-svc PRIVATE hs_common ${HTTPLIB_LIBS})
else()
  message(FATAL_ERROR "cpp-httplib not found; install libcpp-httplib-dev or vcpkg cpp-httplib")
endif()