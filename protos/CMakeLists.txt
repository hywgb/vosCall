cmake_minimum_required(VERSION 3.25)

# Try CONFIG mode; fallback to MODULE mode
find_package(Protobuf QUIET CONFIG)
if(NOT Protobuf_FOUND)
  find_package(Protobuf REQUIRED)
endif()

find_package(gRPC QUIET CONFIG)
if(NOT gRPC_FOUND)
  find_package(gRPC REQUIRED)
endif()

include(FindProtobuf)
include(FindPackageHandleStandardArgs)

set(PROTO_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/route.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/billing.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/cdr.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/auth.proto
  ${CMAKE_CURRENT_SOURCE_DIR}/observe.proto
)

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

foreach(proto ${PROTO_FILES})
  get_filename_component(fname ${proto} NAME_WE)
  protobuf_generate_cpp(${fname}_PB_SRC ${fname}_PB_HDR ${proto} PROTOC_OUT_DIR ${GENERATED_DIR})
  grpc_generate_cpp(${fname}_GRPC_SRC ${fname}_GRPC_HDR ${proto} PROTOC_OUT_DIR ${GENERATED_DIR})
  list(APPEND ALL_PB_SRCS ${${fname}_PB_SRC})
  list(APPEND ALL_PB_HDRS ${${fname}_PB_HDR})
  list(APPEND ALL_GRPC_SRCS ${${fname}_GRPC_SRC})
  list(APPEND ALL_GRPC_HDRS ${${fname}_GRPC_HDR})
endforeach()

add_library(hyperswitch_protos ${ALL_PB_SRCS} ${ALL_GRPC_SRCS})

# Protobuf::libprotobuf target name differs between module/config; use imported vars
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

# absl is not strictly needed; drop explicit absl link for portability

target_include_directories(hyperswitch_protos PUBLIC ${GENERATED_DIR})
target_link_libraries(hyperswitch_protos PUBLIC ${_PROTOBUF_LIB} ${_GRPCPP_LIB})