cmake_minimum_required(VERSION 3.25)

add_executable(admin-api
  src/main.cpp
)

target_include_directories(admin-api PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(nlohmann_json CONFIG REQUIRED)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)

target_link_libraries(admin-api PRIVATE hyperswitch_protos hs_common gRPC::grpc++ protobuf::libprotobuf nlohmann_json::nlohmann_json httplib::httplib)