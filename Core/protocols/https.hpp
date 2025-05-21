#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace SOK {
inline void handle_https(int client_fd, SSL_CTX* ssl_ctx) {
    SSL* ssl = SSL_new(ssl_ctx);
    SSL_set_fd(ssl, client_fd);

    unsigned char peek_buf;
    ssize_t peeked = recv(client_fd, &peek_buf, 1, MSG_PEEK);
    if (peeked != 1 || peek_buf != 0x16) {
        std::cerr << "Non-SSL connection attempt on HTTPS port." << std::endl;
        close(client_fd);
        SSL_free(ssl);
        return;
    }

    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        close(client_fd);
        SSL_free(ssl);
        return;
    }

    // 读取完整HTTPS请求
    std::string request;
    char buf[4096];
    int len;
    while ((len = SSL_read(ssl, buf, sizeof(buf))) > 0) {
        request.append(buf, len);
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }
    if (request.empty()) {
        SSL_shutdown(ssl);
        close(client_fd);
        SSL_free(ssl);
        return;
    }

    // 解析请求行
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    std::cout << "HTTPS Request: " << method << " " << path << " " << version << std::endl;

    // 构造响应
    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 38\r\n\r\nHello, World! This is SOK HTTPS Server!";
    SSL_write(ssl, response.c_str(), response.size());
    SSL_shutdown(ssl);
    close(client_fd);
    SSL_free(ssl);
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