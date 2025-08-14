cmake_minimum_required(VERSION 3.25)

add_library(hs_common
  src/env.cpp
  src/log.cpp
  src/pg.cpp
  src/redis.cpp
)

target_include_directories(hs_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(fmt CONFIG REQUIRED)

# Try vcpkg's unofficial-libpqxx first, then system pqxx
find_package(unofficial-libpqxx CONFIG QUIET)
if(NOT unofficial-libpqxx_FOUND)
  find_package(pqxx CONFIG QUIET)
endif()

# hiredis only (we implemented our own wrapper without redis++)
find_package(hiredis CONFIG QUIET)
if(NOT hiredis_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(HIREDIS REQUIRED hiredis)
    set(HIREDIS_LIB ${HIREDIS_LIBRARIES})
  else()
    set(HIREDIS_LIB hiredis)
  endif()
else()
  set(HIREDIS_LIB hiredis)
endif()

# Resolve pqxx target name
if(TARGET unofficial::libpqxx::pqxx)
  set(PQXX_TARGET unofficial::libpqxx::pqxx)
elseif(TARGET pqxx::pqxx)
  set(PQXX_TARGET pqxx::pqxx)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PQXX REQUIRED libpqxx)
    set(PQXX_TARGET ${PQXX_LIBRARIES})
  else()
    message(FATAL_ERROR "libpqxx (pqxx) not found; install libpqxx-dev or vcpkg unofficial-libpqxx")
  endif()
endif()

# Link
# spdlog: header-only; fmt used elsewhere
# hiredis via pkg-config or module; pqxx resolved above
target_link_libraries(hs_common PUBLIC fmt::fmt ${PQXX_TARGET} ${HIREDIS_LIB})