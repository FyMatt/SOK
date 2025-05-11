#include <iostream>
#include <atomic>
#include <signal.h>
#include "Core/utils/ForkManager.hpp"
#include "Core/utils/SocketUtils.hpp"
#include "Core/mstd/EpollManager.hpp"

std::atomic<bool> running(true);

void signalHandler(int signum) {
    if (signum == SIGINT) {
        running.store(false);
    }
}

/// @brief 注册监听站点
/// @param port 监听请求端口
void siteProcess(int port) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Failed to create epoll instance");
        exit(EXIT_FAILURE);
    }

    int server_fd = setup_server(port);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("Failed to add server_fd to epoll");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "Site process started on port " << port << " (PID: " << getpid() << ")" << std::endl;

    epoll_worker(epoll_fd, server_fd);

    close(server_fd);
    close(epoll_fd);
}

int main() {
    signal(SIGINT, signalHandler);

    SOK::ForkManager forkManager;

    // 启动站点子进程
    forkManager.createChildProcess([] { siteProcess(8081); });
    // forkManager.createChildProcess([] { siteProcess(8082); });

    while (running.load()) {
        std::string command;
        std::cout << "Enter command (restart/exit): ";
        std::cin >> command;

        if (command == "restart") {
            std::cout << "Restarting child processes..." << std::endl;
            forkManager.terminateAll();

            // 重启子进程
            forkManager.createChildProcess([] { siteProcess(8081); });
            // forkManager.createChildProcess([] { siteProcess(8082); });
        } else if (command == "exit") {
            running.store(false);
        }

        // 检查子进程状态
        forkManager.monitorChildren();
    }

    // 终止所有子进程
    forkManager.terminateAll();

    return 0;
}