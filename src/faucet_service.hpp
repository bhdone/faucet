#ifndef FAUCET_SERVICE_HPP
#define FAUCET_SERVICE_HPP

#include <asio.hpp>
#include <plog/Log.h>

#include <memory>
#include <string>
#include <sstream>
#include <functional>
#include <deque>

#include "utils.hpp"

const int MAX_BUF = 1024 * 8;

using asio::ip::tcp;

class SimpleHttpMessageParser {
public:
    SimpleHttpMessageParser() {}

    bool Write(std::string const& msg) {
        m_content += msg;
        bool succ = Parse();
        if (succ) {
            PLOG_DEBUG << "Received: " << std::endl << m_content;
        }
        return succ;
    }

    bool ReadHeader(std::string const& name, std::string& out) const {
        auto i = m_props.find(ToLowerCase(name));
        if (i == std::end(m_props)) {
            return false;
        }
        out = i->second;
        return true;
    }

    std::string ReadBody() const { return m_body; }

    std::string ReadMethodType() const { return m_method_type; }

private:
    void AnalyzeLine(std::string const& line) {
        auto pos = line.find_first_of(':');
        if (pos == std::string::npos) {
            // Analyze method type
            if (m_method_type.empty()) {
                auto p2 = line.find_first_of(' ');
                m_method_type = line.substr(0, p2);
            }
            m_lines.push_back(line);
            return;
        }
        std::string name = ToLowerCase(line.substr(0, pos));
        std::string value = TrimLeftString(line.substr(pos + 1));
        m_props[name] = value;
    }

    bool Parse() {
        auto analyze_from = std::begin(m_content);
        auto p = analyze_from;
        while (p != std::end(m_content)) {
            if (*p == '\r') {
                ++p;
                if (*p == '\n') {
                    ++p;
                    // cut current line
                    std::string line(analyze_from, p - 2);
                    if (line.empty()) {
                        // end of header, following bytes are the content, read length
                        std::string length_str;
                        if (ReadHeader("Content-Length", length_str)) {
                            int length = std::stoi(length_str);
                            std::string content(p, std::end(m_content));
                            if (content.size() < length) {
                                // wrong content
                                return false;
                            }
                            m_body = std::move(content);
                            return true;
                        } else {
                            // cannot find the `Content-Length', there is no data, just return
                            return true;
                        }
                    } else {
                        AnalyzeLine(line);
                        analyze_from = p;
                    }
                }
            } else {
                ++p;
            }
        }
        return false;
    }

private:
    std::string m_content;
    std::vector<std::string> m_lines;
    std::map<std::string, std::string> m_props;
    std::string m_body;
    std::string m_method_type;
};

class SimpleHttpMessageBuilder {
public:
    void WriteContent(std::string const& content, std::string const& content_type) {
        m_ss << "HTTP/1.1 200 OK\r\n";
        m_ss << "Content-Type: " << content_type << "\r\n";
        m_ss << "Content-Length: " << std::to_string(content.size()) << "\r\n";
        m_ss << "\r\n";
        m_ss << content;
    }

    std::string GetMessage() const { return m_ss.str(); }

private:
    std::stringstream m_ss;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    using Callback = std::function<void(bool, SimpleHttpMessageParser const&)>;

    explicit Session(tcp::socket&& s) : m_s(std::move(s)) {}

    ~Session() { PLOGD << "Session is going to be free"; }

    void Start(Callback callback) {
        m_callback = std::move(callback);
        ReadNext();
    }

    void Write(std::string const& msg) {
        bool write = m_writing_msgs.empty();
        m_writing_msgs.push_back(msg);
        if (write) {
            WriteNext();
        }
    }

private:
    void ReadNext() {
        // Start to read until end-of-file
        m_s.async_read_some(
                asio::buffer(m_buf, MAX_BUF),
                [self = shared_from_this()](std::error_code const& ec, std::size_t total_read) {
                    if (ec) {
                        if (ec == asio::error::eof) {
                            // done!
                        } else {
                            PLOG_ERROR << "Peer read error: " << ec.message();
                        }
                        return;
                    }
                    // append all read content to buffer
                    if (self->m_parser.Write(std::string(self->m_buf, self->m_buf + total_read))) {
                        // a whole message is read
                        self->m_callback(true, self->m_parser);
                        return;
                    }
                    self->ReadNext();
                });
    }

    void WriteNext() {
        if (m_writing_msgs.empty()) {
            return;
        }
        std::string const& msg = m_writing_msgs.front();
        asio::async_write(
                m_s, asio::buffer(msg),
                [self = shared_from_this(), &msg](std::error_code const& ec, std::size_t total_wrote) {
                    if (ec) {
                        PLOG_ERROR << "Peer write error: " << ec.message();
                        return;
                    }
                    assert(total_wrote == msg.size());
                    self->m_writing_msgs.pop_front();
                    self->WriteNext();
                });
    }

private:
    char m_buf[MAX_BUF];
    tcp::socket m_s;
    Callback m_callback;
    SimpleHttpMessageParser m_parser;
    std::deque<std::string> m_writing_msgs;
};

class Service {
public:
    using Callback = std::function<void(Session*, SimpleHttpMessageParser const&)>;

    Service(asio::io_context& ioc, tcp::endpoint const& endpoint, Callback callback)
        : m_ioc(ioc), m_acceptor(ioc, endpoint), m_callback(std::move(callback)) {
        AcceptNext();
    }

private:
    void AcceptNext() {
        m_acceptor.async_accept([this](std::error_code const& ec, tcp::socket&& s) {
            if (ec) {
                PLOG_ERROR << "Handle new session error: " << ec.message();
                return;
            }
            auto psession = std::make_shared<Session>(std::move(s));
            psession->Start([this, pweak_session = std::weak_ptr(psession)](bool succ, SimpleHttpMessageParser const& parser) {
                if (succ) {
                    // should pass it to parent
                    auto psession = pweak_session.lock();
                    if (psession) {
                        m_callback(psession.get(), parser);
                    }
                } else {
                    // TODO sorry, it didn't work
                }
            });
            AcceptNext();
        });
    }

private:
    asio::io_context& m_ioc;
    tcp::acceptor m_acceptor;
    Callback m_callback;
};

#endif
