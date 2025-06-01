#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <map>
#include <mutex>

namespace SOK {

int setup_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to set socket options");
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to bind socket");
    }

    if (listen(server_fd, SOMAXCONN) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to listen on socket");
    }

    return server_fd;
}

class FdPortRegistry {
public:
    static FdPortRegistry& instance() {
        static FdPortRegistry inst;
        return inst;
    }

    // 添加fd-port映射，若已存在则不添加，返回是否添加成功
    bool addFdPort(int fd, int port) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fd_port.find(fd);
        if (it != fd_port.end()) return false;
        fd_port[fd] = port;
        return true;
    }

    // 获取fd对应端口，未找到返回-1
    int getPort(int fd) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fd_port.find(fd);
        if (it != fd_port.end()) return it->second;
        return -1;
    }

    // 可选：移除fd
    void removeFd(int fd) {
        std::lock_guard<std::mutex> lock(mtx);
        fd_port.erase(fd);
    }
private:
    std::map<int, int> fd_port;
    std::mutex mtx;
    FdPortRegistry() = default;
    FdPortRegistry(const FdPortRegistry&) = delete;
    FdPortRegistry& operator=(const FdPortRegistry&) = delete;
};

}