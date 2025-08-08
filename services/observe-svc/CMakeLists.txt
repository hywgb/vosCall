cmake_minimum_required(VERSION 3.25)

add_executable(observe-svc
  src/main.cpp
  src/observe_ingest_impl.cpp
)

target_include_directories(observe-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

target_link_libraries(observe-svc PRIVATE hs_common hyperswitch_protos gRPC::grpc++ protobuf::libprotobuf)