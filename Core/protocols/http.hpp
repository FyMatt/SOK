#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>
#include "../utils/Logger.hpp"
#include "../mstd/fileCache.hpp"
#include "../utils/SiteConfig.hpp"
#include <sys/sendfile.h>
#include <fcntl.h>

namespace SOK{
namespace http_util {

/// @brief 更安全的写入函数，处理EPIPE和EAGAIN错误，详细日志
inline bool safe_write(int fd, const void* buf, size_t count) {
    ssize_t ret = write(fd, buf, count);
    if (ret == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true; // 非阻塞下写缓冲满，等待下次 epoll
        }

        SOK_LOG_ERROR("safe_write fd=" + std::to_string(fd) + " count=" + std::to_string(count) + " errno=" + std::to_string(errno) + " msg=" + std::string(strerror(errno)));
        if (errno == EPIPE) {
            return false; // 对端已关闭
        }
    }
    return ret != -1;
}

/// @brief 发送HTTP响应，支持零拷贝sendfile，详细日志
inline void send_http_response(int client_fd, const std::string& version, int status_code, const std::string& status_text,
                              const std::string& mime, const std::string& body, bool keep_alive, const std::string& method, 
                              bool& broken_pipe, const std::string& file_path = "") {
    std::ostringstream oss;
    oss << version << " " << status_code << " " << status_text << "\r\n";
    if (!mime.empty()) oss << "Content-Type: " << mime << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    if (keep_alive) oss << "Connection: keep-alive\r\n";
    oss << "\r\n";
    std::string header = oss.str();
    if (!safe_write(client_fd, header.c_str(), header.size())) {
        SOK_LOG_ERROR("send_http_response header write failed fd=" + std::to_string(client_fd));
        broken_pipe = true;
        return;
    }

    // 静态文件用sendfile零拷贝
    if (method == "GET" && !body.empty() && !file_path.empty()) {
        int fd = open(file_path.c_str(), O_RDONLY);
        if (fd != -1) {
            off_t offset = 0;
            ssize_t sent = 0;
            size_t remain = body.size();
            while (remain > 0) {
                sent = sendfile(client_fd, fd, &offset, remain);
                if (sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        close(fd);
                        return; // 等待下次 epoll
                    }
                    break;
                }
                remain -= sent;
            }
            close(fd);
        }
    } else if (method == "GET" && !body.empty()) {
        size_t total = 0;
        while (total < body.size()) {
            ssize_t ret = write(client_fd, body.data() + total, body.size() - total);
            if (ret == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return; // 等待下次 epoll
                SOK_LOG_ERROR("send_http_response body write failed fd=" + std::to_string(client_fd) + " total=" + std::to_string(total) + " errno=" + std::to_string(errno));
                if (errno == EPIPE) broken_pipe = true;
                return;
            }
            total += ret;
        }
    }
}

/// @brief 解析HTTP头部，返回键值对map
inline std::map<std::string, std::string> parse_headers(std::istringstream& iss) {
    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(iss, line) && line != "\r") {
        auto pos = line.find(":");
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t\r") + 1);
            headers[key] = value;
        }
    }
    return headers;
}

/// @brief 处理HTTP请求，支持keep-alive和零拷贝，write遇到EPIPE时返回false
inline bool handle_http(int client_fd, const SOK::utils::SiteInfo& site_info) {
    try {
        static mstd::FileCache file_cache(1024*1024*50); // 50MB缓存
        bool keep_alive = false;
        bool broken_pipe = false;
        std::string request;
        char buf[4096];
        ssize_t len;
        // 读取请求头
        while ((len = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
            request.append(buf, len);
            if (request.find("\r\n\r\n") != std::string::npos) break; // 头部结束
        }

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true; // 非阻塞下无数据，等待下次 epoll
        }

        if (request.empty()) {
            send_http_response(client_fd, "HTTP/1.1", 400, "Bad Request", "text/plain", "400 Bad Request", false, "GET", broken_pipe);
            SOK_LOG_WARN("Received empty request from client_fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
            return false;
        }

        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;
        if (method.empty() || path.empty() || version.empty()) {
            send_http_response(client_fd, "HTTP/1.1", 400, "Bad Request", "text/plain", "400 Bad Request", false, "GET", broken_pipe);
            SOK_LOG_WARN("Malformed request from client_fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
            return false;
        }

        std::string dummy;
        std::getline(iss, dummy); // 跳过请求行剩余部分
        auto headers = parse_headers(iss);
        auto conn_it = headers.find("Connection");
        if (conn_it != headers.end() && (conn_it->second == "keep-alive" || conn_it->second == "Keep-Alive")) {
            keep_alive = true;
        } else {
            keep_alive = false;
        }

        if (method == "GET" || method == "HEAD") {
            std::string root_dir = site_info.getRootDir();
            std::string file_path = root_dir + path;
            if (file_path == root_dir + "/" || file_path == root_dir) file_path = root_dir + "/index.html";
            // 静态文件缓存查找
            auto file = file_cache.get(file_path);
            if (file) {
                const auto& content = file->first;
                const auto& mime = file->second;
                send_http_response(client_fd, version, 200, "OK", mime, std::string(content.begin(), content.end()), keep_alive, method, broken_pipe, file_path);
            } else {
                send_http_response(client_fd, version, 404, "Not Found", "text/plain", "404 Not Found", keep_alive, method, broken_pipe);
            }
        } else if (method == "POST") {
            std::string body;
            auto pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                body = request.substr(pos + 4);
            }
            send_http_response(client_fd, version, 200, "OK", "text/plain", body, keep_alive, method, broken_pipe);
        } else {
            send_http_response(client_fd, version, 501, "Not Implemented", "text/plain", "501 Not Implemented", keep_alive, method, broken_pipe);
        }

        if (broken_pipe) return false;
        
        return keep_alive;
    } catch(const std::exception& e) {
        bool broken_pipe = false;
        send_http_response(client_fd, "HTTP/1.1", 500, "Internal Server Error", "text/plain", "500 Internal Server Error", false, "GET", broken_pipe);
        SOK_LOG_ERROR(std::string("handle_http exception: ") + e.what() + " for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
        return false;
    } catch(...) {
        bool broken_pipe = false;
        send_http_response(client_fd, "HTTP/1.1", 500, "Internal Server Error", "text/plain", "500 Internal Server Error", false, "GET", broken_pipe);
        SOK_LOG_ERROR("handle_http unknown exception for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
        return false;
    }
}

} // namespace http_util
} // namespace SOK