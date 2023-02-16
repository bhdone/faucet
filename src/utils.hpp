#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <sstream>

#include "types.hpp"

char const hex_chars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

inline char Byte4bToHexChar(uint8_t hex) { return hex_chars[hex]; }

inline std::string ByteToHex(uint8_t byte) {
    std::string hex(2, '0');
    uint8_t hi = (byte & 0xf0) >> 4;
    uint8_t lo = byte & 0x0f;
    hex[0] = Byte4bToHexChar(hi);
    hex[1] = Byte4bToHexChar(lo);
    return hex;
}

inline std::string BytesToHex(Bytes const& bytes) {
    std::stringstream ss;
    for (uint8_t byte : bytes) {
        ss << ByteToHex(byte);
    }
    return ss.str();
}

template <size_t N>
Bytes MakeBytes(std::array<uint8_t, N> const& val) {
    Bytes res(N);
    memcpy(res.data(), val.data(), N);
    return res;
}

inline std::string TrimLeftString(std::string const& str) {
    auto p = str.find_first_not_of(' ');
    if (p == std::string::npos) {
        return "";
    }
    return str.substr(p);
}

#endif
