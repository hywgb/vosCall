cmake_minimum_required(VERSION 3.25)

add_executable(route-svc
  src/main.cpp
  src/route_service_impl.cpp
)

target_include_directories(route-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Try config first, then module
find_package(gRPC QUIET CONFIG)
if(NOT gRPC_FOUND)
  find_package(gRPC REQUIRED)
endif()
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
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

# Reflection lib target name differs
if(TARGET gRPC::grpc++_reflection)
  set(_GRPC_REFLECTION gRPC::grpc++_reflection)
elseif(TARGET grpc++_reflection)
  set(_GRPC_REFLECTION grpc++_reflection)
endif()

target_link_libraries(route-svc PRIVATE hs_common hyperswitch_protos ${_GRPCPP_LIB} ${_PROTOBUF_LIB} fmt::fmt ${_GRPC_REFLECTION})