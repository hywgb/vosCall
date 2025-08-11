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

# Resolve protoc and grpc_cpp_plugin executables
find_program(PROTOC_EXECUTABLE NAMES protoc)
if(NOT PROTOC_EXECUTABLE)
  message(FATAL_ERROR "protoc not found; please install protobuf-compiler or ensure it is on PATH")
endif()
find_program(GRPC_CPP_PLUGIN_EXECUTABLE NAMES grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
  message(FATAL_ERROR "grpc_cpp_plugin not found; please install protobuf-compiler-grpc or ensure it is on PATH")
endif()

set(ALL_SRCS)
set(ALL_HDRS)
foreach(proto ${PROTO_FILES})
  get_filename_component(fname ${proto} NAME_WE)
  set(pb_cc   ${GENERATED_DIR}/${fname}.pb.cc)
  set(pb_hdr  ${GENERATED_DIR}/${fname}.pb.h)
  set(grpc_cc ${GENERATED_DIR}/${fname}.grpc.pb.cc)
  set(grpc_hdr ${GENERATED_DIR}/${fname}.grpc.pb.h)

  add_custom_command(
    OUTPUT ${pb_cc} ${pb_hdr}
    COMMAND ${PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --cpp_out=${GENERATED_DIR}
            ${proto}
    DEPENDS ${proto}
    COMMENT "Generating C++ protobuf sources for ${fname}.proto"
    VERBATIM)

  add_custom_command(
    OUTPUT ${grpc_cc} ${grpc_hdr}
    COMMAND ${PROTOC_EXECUTABLE}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --grpc_out=${GENERATED_DIR}
            --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
            ${proto}
    DEPENDS ${proto}
    COMMENT "Generating gRPC C++ sources for ${fname}.proto"
    VERBATIM)

  list(APPEND ALL_SRCS ${pb_cc} ${grpc_cc})
  list(APPEND ALL_HDRS ${pb_hdr} ${grpc_hdr})
endforeach()

add_library(hyperswitch_protos ${ALL_SRCS})

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
  # Fallback to pkg-config libraries if config targets are not available
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GRPCPP QUIET grpc++)
  endif()
  if(GRPCPP_FOUND)
    set(_GRPCPP_LIB ${GRPCPP_LIBRARIES})
  else()
    message(FATAL_ERROR "No gRPC++ target found")
  endif()
endif()

# absl is not strictly needed; drop explicit absl link for portability

target_include_directories(hyperswitch_protos PUBLIC ${GENERATED_DIR})
target_link_libraries(hyperswitch_protos PUBLIC ${_PROTOBUF_LIB} ${_GRPCPP_LIB})