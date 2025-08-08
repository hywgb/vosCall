cmake_minimum_required(VERSION 3.25)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

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
target_include_directories(hyperswitch_protos PUBLIC ${GENERATED_DIR})
target_link_libraries(hyperswitch_protos PUBLIC protobuf::libprotobuf gRPC::grpc++ absl::base)