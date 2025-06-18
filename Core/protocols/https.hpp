#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <map>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "../utils/Logger.hpp"
#include "../mstd/fileCache.hpp"
#include "../utils/SiteConfig.hpp"
#include <sys/mman.h>
#include <fcntl.h>

namespace SOK {
namespace https_util {

static std::unordered_map<int, SSL*> ssl_map;
static std::mutex ssl_map_mtx;

/// @brief 更安全的 SSL 写入函数，处理 EPIPE、EAGAIN 等错误，并详细日志
inline bool safe_ssl_write(SSL* ssl, const void* buf, int num) {
    int ret = SSL_write(ssl, buf, num);
    if (ret <= 0) {
        int err = SSL_get_error(ssl, ret);
        if ((err == SSL_ERROR_WANT_WRITE) || (err == SSL_ERROR_WANT_READ)) {
            return true; // 非阻塞下写缓冲满，等待下次 epoll
        }
        if (err == SSL_ERROR_SYSCALL && errno == EPIPE) {
            return false; // 对端已关闭
        }
        SOK_LOG_ERROR(std::string("safe_ssl_write failed, SSL_get_error: ") + std::to_string(err));
    }
    return ret > 0;
}

/// @brief 发送 HTTPS 响应，支持静态文件 mmap+SSL_write 零拷贝
inline void send_https_response(SSL* ssl, const std::string& version, int status_code, const std::string& status_text,
                               const std::string& mime, const std::string& body, bool keep_alive, const std::string& method, bool& broken_pipe, const std::string& file_path = "") {
    std::ostringstream oss;
    oss << version << " " << status_code << " " << status_text << "\r\n";
    if (!mime.empty()) oss << "Content-Type: " << mime << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    if (keep_alive) oss << "Connection: keep-alive\r\n";
    oss << "\r\n";
    if (!safe_ssl_write(ssl, oss.str().c_str(), oss.str().size())) {
        broken_pipe = true;
        return;
    }
    if (method == "GET" && !body.empty() && !file_path.empty()) {
        int fd = open(file_path.c_str(), O_RDONLY);
        if (fd != -1) {
            off_t filesize = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            void* file_mem = mmap(nullptr, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
            if (file_mem != MAP_FAILED) {
                size_t total = 0;
                while (total < (size_t)filesize) {
                    int ret = SSL_write(ssl, (char*)file_mem + total, filesize - total);
                    if (ret <= 0) {
                        int err = SSL_get_error(ssl, ret);
                        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                            munmap(file_mem, filesize);
                            close(fd);
                            return; // 等待下次 epoll
                        }
                        SOK_LOG_ERROR("SSL_write failed for static file fd: " + std::to_string(fd));
                        break;
                    }
                    total += ret;
                }
                munmap(file_mem, filesize);
            }
            close(fd);
        }
    } else if (method == "GET" && !body.empty()) {
        if (!safe_ssl_write(ssl, body.data(), body.size())) {
            broken_pipe = true;
            return;
        }
    }
}

/// @brief 解析 HTTP/HTTPS 请求头部
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

/// @brief 处理 HTTPS 连接，支持非阻塞多次 SSL_accept，fd 复用 SSL*
inline bool handle_https(int client_fd, SSL_CTX* ssl_ctx, const SOK::utils::SiteInfo& site_info) {
    try {
        static mstd::FileCache file_cache(1024*1024*50); // 50MB缓存
        SSL* ssl = nullptr;
        bool is_new_ssl = false;
        {
            std::lock_guard<std::mutex> lock(ssl_map_mtx);
            if (ssl_map.count(client_fd) == 0) {
                // 只在新建 SSL* 时判断 0x16
                unsigned char peek_buf;
                ssize_t peeked = recv(client_fd, &peek_buf, 1, MSG_PEEK);
                if (peeked != 1 || peek_buf != 0x16) {
                    SOK_LOG_WARN("Https Received empty or invalid handshake from client_fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
                    return false;
                }
                ssl = SSL_new(ssl_ctx);
                SSL_set_fd(ssl, client_fd);
                ssl_map[client_fd] = ssl;
                is_new_ssl = true;
            } else {
                ssl = ssl_map[client_fd];
            }
        }
        int ret = SSL_accept(ssl);
        if (ret <= 0) {
            int err = SSL_get_error(ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return true;
            }
            SOK_LOG_ERROR("SSL_accept failed for fd: " + std::to_string(client_fd));
            // 释放SSL* 由epoll_worker统一处理
            return false;
        }
        // 握手成功后，直接用 SSL_read 读取 HTTP 请求，不再用 peek 判断
        std::string request;
        char buf[4096];
        int len;
        while ((len = SSL_read(ssl, buf, sizeof(buf))) > 0) {
            request.append(buf, len);
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }
        if (len == 0) {
            // 客户端主动关闭
            if (ssl) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl_map.erase(client_fd);
            }
            return false;
        }
        if (len < 0) {
            int err = SSL_get_error(ssl, len);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // 读未完成，等待下次 epoll
                return true;
            }
        }
        if (request.empty()) {
            SOK_LOG_WARN("Https Received empty request from client_fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
            std::string version = "HTTP/1.1";
            bool broken_pipe = false;
            send_https_response(ssl, version, 400, "Bad Request", "text/plain", "400 Bad Request", false, "GET", broken_pipe);
            if (ssl) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl_map.erase(client_fd);
            }
            return false;
        }
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;
        std::string dummy;
        std::getline(iss, dummy);
        auto headers = parse_headers(iss);
        bool keep_alive = false;
        bool broken_pipe = false;
        auto conn_it = headers.find("Connection");
        if (conn_it != headers.end() && (conn_it->second == "keep-alive" || conn_it->second == "Keep-Alive")) {
            keep_alive = true;
        }
        if (method.empty() || path.empty() || version.empty()) {
            send_https_response(ssl, "HTTP/1.1", 400, "Bad Request", "text/plain", "400 Bad Request", false, method, broken_pipe);
            if (ssl) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl_map.erase(client_fd);
            }
            return false;
        }
        if (method == "GET" || method == "HEAD") {
            std::string root_dir = site_info.getRootDir();
            std::string file_path = root_dir + path;
            if (file_path == root_dir + "/" || file_path == root_dir) file_path = root_dir + "/index.html";
            auto file = file_cache.get(file_path);
            if (file) {
                const auto& content = file->first;
                const auto& mime = file->second;
                send_https_response(ssl, version, 200, "OK", mime, std::string(content.begin(), content.end()), keep_alive, method, broken_pipe, file_path);
            } else {
                send_https_response(ssl, version, 404, "Not Found", "text/plain", "404 Not Found", keep_alive, method, broken_pipe);
            }
        } else if (method == "POST") {
            std::string body;
            auto pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                body = request.substr(pos + 4);
            }
            send_https_response(ssl, version, 200, "OK", "text/plain", body, keep_alive, method, broken_pipe);
        } else {
            send_https_response(ssl, version, 501, "Not Implemented", "text/plain", "501 Not Implemented", keep_alive, method, broken_pipe);
        }
        if (broken_pipe || !keep_alive) {
            if (ssl) {
                SSL_shutdown(ssl);
                SSL_free(ssl);
                ssl_map.erase(client_fd);
            }
            return false;
        }
        // keep-alive 情况下不关闭 SSL，等待下次 epoll
        return true;
    } catch(const std::exception& e) {
        SOK_LOG_ERROR(std::string("handle_https exception: ") + e.what() + " for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
        return false;
    } catch(...) {
        SOK_LOG_ERROR("handle_https unknown exception for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(site_info.getPort()));
        return false;
    }
}

/// @brief 创建 SSL_CTX
inline SSL_CTX* create_ssl_ctx(const char* cert_file, const char* key_file) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    const SSL_METHOD* method = TLS_server_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
    
    if (!SSL_CTX_check_private_key(ctx)) {
        std::cerr << "Private key does not match the certificate public key" << std::endl;
        exit(EXIT_FAILURE);
    }
    return ctx;
}

} // namespace https_util
} // namespace SOK