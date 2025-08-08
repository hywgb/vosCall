cmake_minimum_required(VERSION 3.25)

add_executable(cdr-svc
  src/main.cpp
  src/cdr_ingest_impl.cpp
)

target_include_directories(cdr-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

target_link_libraries(cdr-svc PRIVATE hs_common hyperswitch_protos gRPC::grpc++ protobuf::libprotobuf cpr::cpr)