#pragma once

#include <string>
#include <unistd.h>
#include <vector>
#include <regex>
#include <openssl/ssl.h>
#include "../protocols/http.hpp"
#include "../protocols/https.hpp"
#include "SiteConfig.hpp"

namespace SOK {

/// @brief 根据客户端数据自动分发协议（HTTP/HTTPS），并调用对应处理函数
inline bool dispatch_protocol(int client_fd, int port, SSL_CTX* ssl_ctx) {
    try {
    // 只peek前16字节用于协议判断
    std::vector<char> peek_buf(16);
    ssize_t n = recv(client_fd, peek_buf.data(), peek_buf.size(), MSG_PEEK);
    if (n <= 0) {
        // 客户端关闭或出错
        return false;
    }
    std::string data(peek_buf.data(), n);
    static const std::regex http_regex(R"(^GET |^POST |^HEAD |^PUT |^DELETE |^OPTIONS |^TRACE |^CONNECT |^PATCH |^HTTP/)");
    // 更健壮的 HTTPS 判断：
    // 只要第一个字节是 0x14/0x15/0x16/0x17，且第二字节是 0x03（TLS 1.x），就判定为 HTTPS
    if(n >= 2 &&
       (static_cast<unsigned char>(data[0]) == 0x14 ||
        static_cast<unsigned char>(data[0]) == 0x15 ||
        static_cast<unsigned char>(data[0]) == 0x16 ||
        static_cast<unsigned char>(data[0]) == 0x17) &&
       static_cast<unsigned char>(data[1]) == 0x03) {
        // HTTPS协议，交给handle_https处理
        SOK::utils::SiteInfo site_info(port);
        return SOK::https_util::handle_https(client_fd, ssl_ctx, site_info);
    } else if (std::regex_search(data, http_regex)) {
        // HTTP协议，交给handle_http处理
        SOK::utils::SiteInfo site_info(port);
        return SOK::http_util::handle_http(client_fd, site_info);
    } else {
        // 其他协议可扩展
        SOK_LOG_WARN("Unsupported protocol or malformed request from client_fd: " + 
            std::to_string(client_fd) + " on port: " + std::to_string(port));
        // 打印peek内容为hex
        std::ostringstream oss;
        oss << std::hex;
        for (int i = 0; i < n; ++i) {
            oss << (static_cast<unsigned int>(static_cast<unsigned char>(peek_buf[i])));
            if (i != n-1) oss << " ";
        }
        SOK_LOG_WARN("peeked data (hex): " + oss.str());
        return false;
    }
    } catch(const std::exception& e) {
        SOK_LOG_ERROR(std::string("dispatch_protocol exception: ") + e.what() + " for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(port));
        return false;
    } catch(...) {
        SOK_LOG_ERROR("dispatch_protocol unknown exception for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(port));
        return false;
    }
}

} // namespace SOK