#pragma once
#include <string>
#include <unistd.h>
#include <sstream>
#include <iostream>

namespace SOK {
inline void handle_http(int client_fd) {
    // 读取完整HTTP请求
    std::string request;
    char buf[4096];
    ssize_t len;
    while ((len = recv(client_fd, buf, sizeof(buf), 0)) > 0) {
        request.append(buf, len);
        if (request.find("\r\n\r\n") != std::string::npos) break; // 头部结束
    }
    if (request.empty()) {
        close(client_fd);
        return;
    }

    // 解析请求行
    std::istringstream iss(request);
    std::string method, path, version;
    iss >> method >> path >> version;

    std::cout << "HTTP Request: " << method << " " << path << " " << version << std::endl;
    std::cout << "Request Headers Content: " << request << std::endl;


    // 请求内容
    std::string content;
    

    // 构造响应
    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 37\r\n\r\nHello, World! This is SOK Web Server!";
    write(client_fd, response.c_str(), response.size());
    close(client_fd);
}

}