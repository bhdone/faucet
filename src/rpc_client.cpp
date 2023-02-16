#include "rpc_client.h"

#include <fstream>
#include <iostream>

#include <filesystem>
namespace fs = std::filesystem;

#include "utils.hpp"

namespace miner {

RPCClient::RPCClient(bool no_proxy, std::string url, std::string const& cookie_path_str)
        : m_no_proxy(no_proxy), m_url(std::move(url)) {
    if (cookie_path_str.empty()) {
        throw std::runtime_error("cookie is empty, cannot connect to btchd core");
    }

    fs::path cookie_path(cookie_path_str);
    std::ifstream cookie_reader(cookie_path.string());
    if (!cookie_reader.is_open()) {
        std::stringstream ss;
        ss << "cannot open to read " << cookie_path;
        throw std::runtime_error(ss.str());
    }
    std::string auth_str;
    std::getline(cookie_reader, auth_str);
    if (auth_str.empty()) {
        throw std::runtime_error("cannot read auth string from `.cookie`");
    }
    auto pos = auth_str.find_first_of(':');
    std::string user_str = auth_str.substr(0, pos);
    std::string passwd_str = auth_str.substr(pos + 1);
    m_user = std::move(user_str);
    m_passwd = std::move(passwd_str);
}

RPCClient::RPCClient(bool no_proxy, std::string url, std::string user, std::string passwd)
        : m_no_proxy(no_proxy), m_url(std::move(url)), m_user(std::move(user)), m_passwd(std::move(passwd)) {}

void RPCClient::BuildRPCJson(Json::Value& params, std::string const& val) {
    params.append(val);
}

void RPCClient::BuildRPCJson(Json::Value& params, Bytes const& val) {
    params.append(BytesToHex(val));
}

void RPCClient::BuildRPCJson(Json::Value& params, bool val) { params.append(Json::Value(val)); }

void RPCClient::BuildRPCJsonWithParams(Json::Value& out_params) {}

}  // namespace miner
