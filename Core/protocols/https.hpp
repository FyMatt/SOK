#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <map>
#include <vector>
#include "../utils/Logger.hpp"
#include "../mstd/fileCache.hpp"
#include "../utils/SiteConfig.hpp"

namespace SOK {
namespace https_util {

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

inline bool handle_https(int client_fd, SSL_CTX* ssl_ctx, const SOK::utils::SiteInfo& site_info) {
    // SOK_LOG_INFO("http client_fd: " + std::to_string(client_fd) + "\n http port: " + std::to_string(site_info.getPort()));
    // SOK_LOG_INFO("Site root: " + site_info.getRootDir());
    // SOK_LOG_INFO("Site name: " + site_info.getSiteName());
    static mstd::FileCache file_cache(1024*1024*50); // 50MB缓存
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, client_fd);

    unsigned char peek_buf;
    ssize_t peeked = recv(client_fd, &peek_buf, 1, MSG_PEEK);
    if (peeked != 1 || peek_buf != 0x16) {
        SOK_LOG_ERROR("Not a valid SSL/TLS connection");
        SSL_free(ssl);
        return false;
    }

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return false;
    }

    bool keep_alive = false;
    do {
        // 读取完整HTTPS请求
        std::string request;
        char buf[4096];
        int len;
        while ((len = SSL_read(ssl, buf, sizeof(buf))) > 0) {
            request.append(buf, len);
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }
        if (request.empty()) {
            break;
        }

        // 解析请求行和头部
        std::istringstream iss(request);
        std::string method, path, version;
        iss >> method >> path >> version;
        std::string dummy;
        std::getline(iss, dummy);
        auto headers = parse_headers(iss);

        auto conn_it = headers.find("Connection");
        if (conn_it != headers.end() && (conn_it->second == "keep-alive" || conn_it->second == "Keep-Alive")) {
            keep_alive = true;
        } else {
            keep_alive = false;
        }

        if (method == "GET" || method == "HEAD") {
            // 使用site_info.getRootDir()作为根目录
            std::string root_dir = site_info.getRootDir();
            std::string file_path = root_dir + path;
            if (file_path == root_dir + "/" || file_path == root_dir) file_path = root_dir + "/index.html";
            SOK_LOG_INFO("Http Request: " + method + " " + path);
            SOK_LOG_INFO("find file_path: " + file_path);
            auto file = file_cache.get(file_path);
            if (file) {
                const auto& content = file->first;
                const auto& mime = file->second;
                std::ostringstream oss;
                oss << version << " 200 OK\r\nContent-Type: " << mime << "\r\nContent-Length: " << content.size() << "\r\n";
                if (keep_alive) oss << "Connection: keep-alive\r\n";
                oss << "\r\n";
                SSL_write(ssl, oss.str().c_str(), oss.str().size());
                if (method == "GET") {
                    SSL_write(ssl, content.data(), content.size());
                }
            } else {
                std::string not_found = version + " 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
                SSL_write(ssl, not_found.c_str(), not_found.size());
            }
        } else if (method == "POST") {
            std::string body;
            auto pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                body = request.substr(pos + 4);
            }
            std::ostringstream oss;
            oss << version << " 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " << body.size() << "\r\n";
            if (keep_alive) oss << "Connection: keep-alive\r\n";
            oss << "\r\n" << body;
            SSL_write(ssl, oss.str().c_str(), oss.str().size());
        } else {
            std::string not_impl = version + " 501 Not Implemented\r\nContent-Length: 18\r\n\r\n501 Not Implemented";
            SSL_write(ssl, not_impl.c_str(), not_impl.size());
        }
    } while (keep_alive);
    SSL_shutdown(ssl);
    SSL_free(ssl);
    return keep_alive;
}

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
        ERR_print_errors_fp(stderr);      exit(EXIT_FAILURE);
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

}
}