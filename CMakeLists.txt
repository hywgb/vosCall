cmake_minimum_required(VERSION 3.25)
project(hyperswitch CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# vcpkg toolchain if provided
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# Enable LTO in Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT lto_supported OUTPUT output)
  if(lto_supported)
    message(STATUS "IPO/LTO enabled")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

add_subdirectory(protos)
add_subdirectory(common)
add_subdirectory(services/route-svc)
add_subdirectory(services/cdr-svc)
add_subdirectory(services/billing-svc)
add_subdirectory(apps/admin-api)
add_subdirectory(tools/loader)