#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include "../mstd/fileCache.hpp"
#include "../utils/Logger.hpp"
#include <map>
#include <vector>

namespace SOK{
namespace http_util {

inline std::map<std::string, std::string> parse_headers(std::istringstream& iss) {
    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        auto pos = line.find(":");
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            headers[key] = value;
        }
    }
    return headers;
}

inline void handle_http(int client_fd) {
    static mstd::FileCache file_cache(1024*1024*50); // 50MB缓存
    bool keep_alive = false;
    do {
        // 读取完整HTTP请求
        std::string request;
        char buf[4096];
        ssize_t len;
        while ((len = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
            request.append(buf, len);
            if (request.find("\r\n\r\n") != std::string::npos) break; // 头部结束
        }
        if (request.empty()) {
            break;
        }

        // 解析请求行和头部
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;
        std::string dummy;
        std::getline(iss, dummy); // 跳过请求行剩余部分
        auto headers = parse_headers(iss);

        // 判断keep-alive
        auto conn_it = headers.find("Connection");
        if (conn_it != headers.end() && (conn_it->second == "keep-alive" || conn_it->second == "Keep-Alive")) {
            keep_alive = true;
        } else {
            keep_alive = false;
        }

        // 处理GET/HEAD/POST
        if (method == "GET" || method == "HEAD") {
            std::string file_path = "." + path;
            if (file_path == "./") file_path = "./index.html";
            SOK::Logger::instance().info("HTTP Request: " + method + " " + file_path);
            auto file = file_cache.get(file_path);
            if (file) {
                const auto& content = file->first;
                const auto& mime = file->second;
                std::ostringstream oss;
                oss << version << " 200 OK\r\nContent-Type: " << mime << "\r\nContent-Length: " << content.size() << "\r\n";
                if (keep_alive) oss << "Connection: keep-alive\r\n";
                oss << "\r\n";
                write(client_fd, oss.str().c_str(), oss.str().size()); // 发送HTTP响应头
                if (method == "GET") {
                    write(client_fd, content.data(), content.size()); // 发送文件内容（响应体）
                }
            } else {
                std::string not_found = version + " 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                write(client_fd, not_found.c_str(), not_found.size());
            }
        } else if (method == "POST") {
            // 简单回显POST内容
            std::string body;
            auto pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                body = request.substr(pos + 4);
            }
            std::ostringstream oss;
            oss << version << " 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " << body.size() << "\r\n";
            if (keep_alive) oss << "Connection: keep-alive\r\n";
            oss << "\r\n" << body;
            write(client_fd, oss.str().c_str(), oss.str().size());
        } else {
            std::string not_impl = version + " 501 Not Implemented\r\nContent-Length: 18\r\n\r\n501 Not Implemented";
            write(client_fd, not_impl.c_str(), not_impl.size());
        }
    } while (keep_alive);
    close(client_fd);
}

}
}