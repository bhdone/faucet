cmake_minimum_required(VERSION 3.0)

project(btchd-faucet)

find_package(plog CONFIG REQUIRED)
find_package(cxxopts CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

set(BF_SRCS
    src/main.cpp
    src/http_client.cpp
    src/rpc_client.cpp
)

add_executable(btchd-faucet ${BF_SRCS})
target_link_libraries(btchd-faucet PRIVATE plog::plog cxxopts::cxxopts CURL::libcurl JsonCpp::JsonCpp asio asio::asio)
target_compile_features(btchd-faucet PRIVATE cxx_std_17)
