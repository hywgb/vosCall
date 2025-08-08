cmake_minimum_required(VERSION 3.25)

add_executable(route-svc
  src/main.cpp
  src/route_service_impl.cpp
)

target_include_directories(route-svc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

target_link_libraries(route-svc PRIVATE hs_common hyperswitch_protos gRPC::grpc++ protobuf::libprotobuf)