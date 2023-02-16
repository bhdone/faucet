#ifndef BHD_MINER_RPC_CLIENT_HPP
#define BHD_MINER_RPC_CLIENT_HPP

#include <curl/curl.h>
#include <plog/Log.h>
#include <json/value.h>
#include <json/reader.h>

#include <cstdint>
#include <string>

#include "http_client.h"

#include "utils.hpp"

namespace miner {

class Error : public std::runtime_error {
public:
    explicit Error(char const* msg) : std::runtime_error(msg) {}
};

class NetError : public Error {
public:
    explicit NetError(char const* msg) : Error(msg) {}
};

class RPCError : public Error {
public:
    RPCError(int code, std::string msg) : Error(msg.c_str()), m_code(code), m_msg(std::move(msg)) {}

    int GetCode() const { return m_code; }

private:
    int m_code;
    std::string m_msg;
};

class RPCClient {
public:
    struct Result {
        Json::Value result;
        std::string error;
        int id;
    };

    RPCClient(bool no_proxy, std::string url, std::string const& cookie_path_str = "");

    RPCClient(bool no_proxy, std::string url, std::string user, std::string passwd);

private:
    void BuildRPCJson(Json::Value& params, std::string const& val);

    void BuildRPCJson(Json::Value& params, Bytes const& val);

    void BuildRPCJson(Json::Value& params, bool val);

    template <size_t N>
    void BuildRPCJson(Json::Value& params, std::array<uint8_t, N> const& val) {
        BuildRPCJson(params, BytesToHex(MakeBytes(val)));
    }

    template <typename T>
    void BuildRPCJson(Json::Value& params, std::vector<T> const& val) {
        Json::Value res(Json::arrayValue);
        for (auto const& v : val) {
            BuildRPCJson(res, v);
        }
        params.append(res);
    }

    template <typename T>
    void BuildRPCJson(Json::Value& params, T&& val) {
        params.append(std::forward<T>(val));
    }

    void BuildRPCJsonWithParams(Json::Value& out_params);

    template <typename V, typename... T>
    void BuildRPCJsonWithParams(Json::Value& outParams, V&& val, T&&... vals) {
        BuildRPCJson(outParams, std::forward<V>(val));
        BuildRPCJsonWithParams(outParams, std::forward<T>(vals)...);
    }

    template <typename... T>
    Result SendMethod(bool no_proxy, std::string const& method_name, T&&... vals) {
        Json::Value root;
        root["jsonrpc"] = "2.0";
        root["method"] = method_name;
        Json::Value params(Json::arrayValue);
        BuildRPCJsonWithParams(params, std::forward<T>(vals)...);
        root["params"] = params;
        // Invoke curl
        HTTPClient client(m_url, m_user, m_passwd, no_proxy);
        std::string send_str = root.toStyledString();
        PLOG_DEBUG << "sending: `" << send_str << "`";
        bool succ;
        int code;
        std::string err_str;
        std::tie(succ, code, err_str) = client.Send(send_str);
        if (!succ) {
            std::stringstream ss;
            ss << "RPC command error `" << method_name << "`: " << err_str;
            throw NetError(ss.str().c_str());
        }
        // Analyze the result
        Bytes received_data = client.GetReceivedData();
        if (received_data.empty()) {
            throw NetError("empty result from RPC server");
        }
        char const* psz = reinterpret_cast<char const*>(received_data.data());
        Json::Value res;
        Json::CharReaderBuilder builder;
        auto reader = std::make_shared<Json::CharReader>(builder.newCharReader());
        std::string errs;
        if (!reader->parse(psz, psz + strlen(psz), &res, &errs)) {
            throw RPCError("cannot parse the result from rpc server");
        }

        // Build result and return
        PLOG_DEBUG << "received: `" << res.toStyledString() << "`";
        Result result;
        if (res.isMember("result")) {
            result.result = res["result"];
        }
        if (res.isMember("error") && !res["error"].isNull()) {
            Json::Value errorJson = res["error"];
            int code = errorJson["code"].asInt();
            std::string msg = errorJson["message"].asString();
            throw RPCError(code, msg);
        }
        if (res.isMember("id") && res["id"].isInt()) {
            result.id = res["id"].asInt();
        }
        return result;
    }

private:
    bool m_no_proxy;
    std::string m_url;
    std::string m_user;
    std::string m_passwd;
};

}  // namespace miner

#endif
