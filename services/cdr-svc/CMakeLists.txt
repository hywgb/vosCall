cmake_minimum_required(VERSION 3.25)

add_executable(cdr-svc
  src/main.cpp
  src/cdr_ingest_impl.cpp
)

target_include_directories(cdr-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Try config first, then module
find_package(gRPC QUIET CONFIG)
if(NOT gRPC_FOUND)
  find_package(gRPC REQUIRED)
endif()
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

# Find httplib via config or pkg-config
find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(HTTPLIB REQUIRED cpp-httplib)
    set(HTTPLIB_LIBS ${HTTPLIB_LIBRARIES})
  endif()
endif()

find_package(fmt CONFIG REQUIRED)

# Resolve protobuf target
if(TARGET protobuf::libprotobuf)
  set(_PROTOBUF_LIB protobuf::libprotobuf)
elseif(TARGET Protobuf::libprotobuf)
  set(_PROTOBUF_LIB Protobuf::libprotobuf)
elseif(TARGET Protobuf::protobuf)
  set(_PROTOBUF_LIB Protobuf::protobuf)
else()
  message(FATAL_ERROR "No protobuf target found")
endif()

if(TARGET gRPC::grpc++)
  set(_GRPCPP_LIB gRPC::grpc++)
elseif(TARGET grpc++)
  set(_GRPCPP_LIB grpc++)
else()
  message(FATAL_ERROR "No gRPC++ target found")
endif()

if(TARGET httplib::httplib)
  target_link_libraries(cdr-svc PRIVATE hs_common hyperswitch_protos ${_GRPCPP_LIB} ${_PROTOBUF_LIB} fmt::fmt httplib::httplib)
elseif(DEFINED HTTPLIB_LIBS)
  target_link_libraries(cdr-svc PRIVATE hs_common hyperswitch_protos ${_GRPCPP_LIB} ${_PROTOBUF_LIB} fmt::fmt ${HTTPLIB_LIBS})
else()
  message(FATAL_ERROR "cpp-httplib not found; install libcpp-httplib-dev or vcpkg cpp-httplib")
endif()