cmake_minimum_required(VERSION 3.25)

add_executable(loader
  src/main.cpp
)

# Include common headers
target_include_directories(loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/common/include)

find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(unofficial-libpqxx CONFIG QUIET)
if(NOT unofficial-libpqxx_FOUND)
  find_package(pqxx CONFIG QUIET)
endif()

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
    message(FATAL_ERROR "libpqxx not found")
  endif()
endif()

# Link with hs_common to reuse Pg and logging
target_link_libraries(loader PRIVATE hs_common spdlog::spdlog fmt::fmt ${PQXX_TARGET})