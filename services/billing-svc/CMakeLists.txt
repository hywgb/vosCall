cmake_minimum_required(VERSION 3.25)

add_executable(billing-svc
  src/main.cpp
  src/billing_service_impl.cpp
)

target_include_directories(billing-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

target_link_libraries(billing-svc PRIVATE hs_common hyperswitch_protos gRPC::grpc++ protobuf::libprotobuf)