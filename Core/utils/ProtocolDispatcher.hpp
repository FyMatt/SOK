#pragma once

#include <string>
#include <unistd.h>
#include <vector>
#include <regex>
#include <iostream>
#include <openssl/ssl.h>
#include "../protocols/http.hpp"
#include "../protocols/https.hpp"

namespace SOK {

inline void dispatch_protocol(int client_fd) {
    // 只peek前16字节用于协议判断
    std::vector<char> peek_buf(16);
    ssize_t n = recv(client_fd, peek_buf.data(), peek_buf.size(), MSG_PEEK);
    if (n <= 0) {
        // 客户端关闭或出错
        close(client_fd);
        return;
    }
    std::string data(peek_buf.data(), n);

    static const std::regex http_regex(R"(^GET |^POST |^HEAD |^PUT |^DELETE |^OPTIONS |^TRACE |^CONNECT |^PATCH |^HTTP/)");
    
    if(n >= 3 &&
               static_cast<unsigned char>(data[0]) == 0x16 &&
               static_cast<unsigned char>(data[1]) == 0x03 &&
               (static_cast<unsigned char>(data[2]) >= 0x00 && static_cast<unsigned char>(data[2]) <= 0x04)) {
        // HTTPS协议，交给handle_https处理（handle_https里负责SSL握手和完整读取、解析）
        SSL_CTX* ssl_ctx = SOK::create_ssl_ctx("server.crt", "server.key");
        handle_https(client_fd, ssl_ctx);
    }else if (std::regex_search(data, http_regex)) {
        // HTTP协议，交给handle_http处理（handle_http里负责完整读取和解析）
        handle_http(client_fd);
    } else {
        // 其他协议可扩展
        // handle_proxy(client_fd); 或 handle_loadbalance(client_fd);
        close(client_fd);
        return;
    }
}

}