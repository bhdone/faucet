#include <iostream>
#include <string>

#include <cxxopts.hpp>

#include <plog/Log.h>
#include <plog/Initializers/ConsoleInitializer.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include <json/reader.h>
#include <json/json.h>
#include <json/value.h>

#include "faucet_service.hpp"
#include "rpc_client.h"

int main(int argc, char const* argv[]) {
    cxxopts::Options opts(
            "btchd-faucet", "Provide a service that can send amount to BHD address with countable management.");
    opts.add_options()                      // All options here
            ("help", "Show help document")  // --help
            ("rpc-url", "Connect to btchd through this url",
             cxxopts::value<std::string>()->default_value("http://127.0.0.1:18732"))  // --rpc-url
            ("cookie-path", "The path to `.cookie`",
             cxxopts::value<std::string>()->default_value("~/.btchd/testnet3/.cookie"))  // --cookie-path
            ("addr", "Service will bind to this address",
             cxxopts::value<std::string>()->default_value("0.0.0.0"))  // --addr
            ("port", "Service will bind to this port",
             cxxopts::value<unsigned short>()->default_value("18080"))  // --port
            ("verbose", "Show more logs for debugging purpose")         // --verbose
            ("amount", "How many BHD we should send to user on each request",
             cxxopts::value<int>()->default_value("10"))  // --amount
            ;
    auto result = opts.parse(argc, argv);
    if (result.count("help")) {
        std::cout << opts.help() << std::endl;
        return 0;
    }
    auto log_type = result.count("verbose") ? plog::Severity::debug : plog::Severity::info;
    // Initialize log system
    plog::ConsoleAppender<plog::TxtFormatter> appender;
    plog::init(log_type, &appender);
    PLOG_INFO << "Faucet for BitcoinHD testnet3";

    std::string rpc_url = result["rpc-url"].as<std::string>();
    std::string cookie_path = result["cookie-path"].as<std::string>();
    RPCClient rpc(true, rpc_url, cookie_path);

    int amount = result["amount"].as<int>();

    std::string addr = result["addr"].as<std::string>();
    unsigned short port = result["port"].as<unsigned short>();

    PLOG_INFO << "Initializing service, bind " << addr << ", port " << port;
    tcp::endpoint endpoint(asio::ip::address::from_string(addr), port);

    asio::io_context ioc;
    Service service(ioc, endpoint, [&rpc, amount](Session* psession, SimpleHttpMessageParser const& parser) {
        // analyze the received string and trying to return the tx id
        SimpleHttpMessageBuilder msg_builder;
        std::string content_type;
        if (!parser.ReadHeader("Content-Type", content_type)) {
            msg_builder.WriteContent("Missing `Content-Type`.", "text/html");
        } else if (content_type != "application/json") {
            msg_builder.WriteContent("Invalid Content-Type, `application/json` is required.", "text/html");
        }
        // parse the json from content
        Json::CharReaderBuilder builder;
        Json::CharReader* reader = builder.newCharReader();
        std::string body = parser.ReadBody();
        Json::Value root;
        std::string errs;
        if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs)) {
            msg_builder.WriteContent("Cannot parse json!", "text/html");
            psession->Write(msg_builder.GetMessage());
            return;
        }
        if (!root.isMember("address")) {
            msg_builder.WriteContent("No `address` can be found!", "text/html");
            psession->Write(msg_builder.GetMessage());
            return;
        }
        std::string address = root["address"].asString();
        // invoke RPC and send the amount
        std::string tx_str = rpc.SendToAddress(address, amount);
        msg_builder.WriteContent(tx_str, "text/html");
        psession->Write(msg_builder.GetMessage());
    });
    ioc.run();
    return 0;
}
