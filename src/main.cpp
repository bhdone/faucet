#include <iostream>
#include <fstream>
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

class FaucetAddrMan {
public:
    bool SaveToFile(std::string const& path) {
        Json::Value root(Json::arrayValue);
        for (auto i = std::begin(m_records); i != std::end(m_records); ++i) {
            Json::Value r;
            r["address"] = i->first;
            r["time"] = i->second;
            root.append(r);
        }
        // open file
        std::ofstream out(path);
        if (!out.is_open()) {
            PLOG_ERROR << "cannot open file to write: " << path;
            return false;
        }
        out << root.toStyledString();
        return true;
    }

    bool LoadFromFile(std::string const& path) {
        std::ifstream in(path);
        if (!in.is_open()) {
            PLOG_ERROR << "cannot open file to read: " << path;
            return false;
        }
        // length
        in.seekg(0, std::ifstream::end);
        int len = in.tellg();
        in.seekg(0);
        std::shared_ptr<char> json_str(new char[len + 1], [](char* p) { delete[] p; });
        in.read(json_str.get(), len);
        json_str.get()[len] = '\0';
        // parse to json
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value root;
        std::string errs;
        if (!reader->parse(json_str.get(), json_str.get() + len, &root, &errs)) {
            // cannot parse the json by the content of file
            return false;
        }
        if (!root.isArray()) {
            return false;
        }
        m_records.clear();
        for (auto const& record : root) {
            if (!record.isMember("address") || !record["address"].isString()) {
                continue;
            }
            if (!record.isMember("time") || !record["time"].isInt()) {
                continue;
            }
            std::string address_str = record["address"].asString();
            int time = record["time"].asInt();
            m_records[address_str] = time;
        }
        PLOG_DEBUG << "read total " << m_records.size() << " record(s) from db file";
        return true;
    }

    void Update(std::string const& addr) {
        auto curr = time(nullptr);
        m_records[addr] = curr;
    }

    int Query(std::string const& addr) {
        auto i = m_records.find(addr);
        if (i == std::end(m_records)) {
            return 0;
        }
        return i->second;
    }

private:
    std::map<std::string, int> m_records;
};

int main(int argc, char const* argv[]) {
    cxxopts::Options opts(
            "btchd-faucet", "Provide a service that can send amount to BHD address with countable management.");
    opts.add_options()                      // All options here
            ("help", "Show help document")  // --help
            ("rpc-url", "Connect to btchd through this url",
             cxxopts::value<std::string>()->default_value("http://127.0.0.1:18732"))  // --rpc-url
            ("cookie-path", "The path to `.cookie`",
             cxxopts::value<std::string>()->default_value("$HOME/.btchd/testnet3/.cookie"))  // --cookie-path
            ("addr", "Service will bind to this address",
             cxxopts::value<std::string>()->default_value("0.0.0.0"))  // --addr
            ("port", "Service will bind to this port",
             cxxopts::value<unsigned short>()->default_value("18080"))  // --port
            ("verbose", "Show more logs for debugging purpose")         // --verbose
            ("amount", "How many BHD we should send to user on each request",
             cxxopts::value<int>()->default_value("10"))  // --amount
            ("db-path", "The database file stores all funded addresses",
             cxxopts::value<std::string>()->default_value("faucet-db.json"))  // --db
            ("secs-on-next-fund", "How many seconds should be taken for the same address can be funded again?",
             cxxopts::value<int>()->default_value("60"))  // --secs-on-next-fund
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
    std::string cookie_path = ExpandEnvPath(result["cookie-path"].as<std::string>());
    PLOG_DEBUG << "Construct RPC object with url: " << rpc_url << ", cookie: " << cookie_path;
    RPCClient rpc(true, rpc_url, cookie_path);

    int amount = result["amount"].as<int>();

    std::string addr = result["addr"].as<std::string>();
    unsigned short port = result["port"].as<unsigned short>();

    FaucetAddrMan addr_man;
    std::string db_path = ExpandEnvPath(result["db-path"].as<std::string>());
    addr_man.LoadFromFile(db_path);

    int secs_on_next_fund = result["secs-on-next-fund"].as<int>();

    PLOG_INFO << "Initializing service, bind " << addr << ", port " << port;
    tcp::endpoint endpoint(asio::ip::address::from_string(addr), port);

    asio::io_context ioc;
    Service service(
            ioc, endpoint,
            [&rpc, amount, &addr_man, &db_path, secs_on_next_fund](
                    Session* psession, SimpleHttpMessageParser const& parser) {
                PLOG_DEBUG << "Processing message...";
                // analyze the received string and trying to return the tx id
                SimpleHttpMessageBuilder msg_builder;
                std::string content_type;
                if (!parser.ReadHeader("Content-Type", content_type)) {
                    PLOG_ERROR << "Message is received without `Content-Type`, ignored.";
                    msg_builder.WriteContent("Missing `Content-Type`.", "text/html");
                    psession->Write(msg_builder.GetMessage());
                    return;
                }
                if (content_type != "application/json") {
                    PLOG_ERROR << "Message is received with an invalid `Content-Type`: " << content_type;
                    msg_builder.WriteContent("Invalid Content-Type, `application/json` is required.", "text/html");
                    psession->Write(msg_builder.GetMessage());
                    return;
                }
                // parse the json from content
                Json::CharReaderBuilder builder;
                Json::CharReader* reader = builder.newCharReader();
                std::string body = parser.ReadBody();
                Json::Value root;
                std::string errs;
                if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs)) {
                    PLOG_ERROR << "Cannot parse json from the message.";
                    msg_builder.WriteContent("Cannot parse json!", "text/html");
                    psession->Write(msg_builder.GetMessage());
                    return;
                }
                if (!root.isMember("address")) {
                    PLOG_ERROR << "No `address` can be found.";
                    msg_builder.WriteContent("No `address` can be found!", "text/html");
                    psession->Write(msg_builder.GetMessage());
                    return;
                }
                std::string address = root["address"].asString();
                // check before invoke RPC
                int fund_time = addr_man.Query(address);
                if (fund_time != 0) {
                    int secs = time(nullptr) - fund_time;
                    if (secs < secs_on_next_fund) {
                        std::stringstream ss;
                        ss << "Address " << address << " already funded " << secs << " seconds ago";
                        PLOG_ERROR << ss.str();
                        msg_builder.WriteContent(ss.str(), "text/html");
                        psession->Write(msg_builder.GetMessage());
                        return;
                    }
                }
                // invoke RPC and send the amount
                PLOG_INFO << "Distribute fund " << amount << "BHD to address `" << address << "`";
                try {
                    std::string tx_str = rpc.SendToAddress(address, amount);
                    addr_man.Update(address);
                    PLOG_INFO << "tx=" << tx_str;
                    if (!addr_man.SaveToFile(db_path)) {
                        PLOG_ERROR << "Cannot write db file: " << db_path;
                    }
                    msg_builder.WriteContent(tx_str, "text/html");
                } catch (std::exception const& e) {
                    msg_builder.WriteContent(e.what(), "text/html");
                }
                psession->Write(msg_builder.GetMessage());
            });
    ioc.run();
    return 0;
}
