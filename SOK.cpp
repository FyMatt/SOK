#include <iostream>
#include <atomic>
#include <signal.h>
#include <thread>
#include <vector>
#include <algorithm>
#include "Core/utils/ForkManager.hpp"
#include "Core/utils/SocketUtils.hpp"
#include "Core/mstd/EpollManager.hpp"

std::atomic<bool> running(true);

void signalHandler(int signum) {
    if (signum == SIGINT) {
        running.store(false);
    }
}

/// @brief 每个进程监听一组端口
/// @param ports 要监听的端口列表
void processWorker(const std::vector<int>& ports) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Failed to create epoll instance");
        exit(EXIT_FAILURE);
    }

    std::vector<int> server_fds;
    for (int port : ports) {
        int server_fd = setup_server(port);
        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            perror("Failed to add server_fd to epoll");
            close(server_fd);
            continue;
        }
        server_fds.push_back(server_fd);
        std::cout << "Process " << getpid() << " listening on port " << port << std::endl;
    }
    epoll_worker(epoll_fd); // -1 表示不需要特定的 server_fd

    for (int fd : server_fds) {
        close(fd);
    }
    close(epoll_fd);
}

int main() {
    signal(SIGINT, signalHandler);

    SOK::ForkManager forkManager;

    // 获取 CPU 核心数
    int cpu_cores = std::thread::hardware_concurrency();
    std::cout << "Detected " << cpu_cores << " CPU cores." << std::endl;

    // 要监听的端口列表
    std::vector<int> ports = {
        8081, 8082};
    std::cout << "Listening on ports: ";
    for (int port : ports) {
        std::cout << port << " ";
    }
    std::cout << std::endl;

    // 将端口分配给每个进程
    int ports_per_process = (ports.size() + cpu_cores - 1) / cpu_cores; // 向上取整
    auto it = ports.begin();

    for (int i = 0; i < cpu_cores && it != ports.end(); ++i) {
        auto end_it = std::next(it, std::min(ports_per_process, static_cast<int>(std::distance(it, ports.end()))));
        std::vector<int> process_ports(it, end_it);
        it = end_it;

        forkManager.createChildProcess([process_ports] {
            processWorker(process_ports);
        });
    }

    while (running.load()) {
        std::string command;
        std::cout << "Enter command (restart/exit): ";
        std::cin >> command;

        if (command == "restart") {
            std::cout << "Restarting child processes..." << std::endl;
            forkManager.terminateAll();

            // 重新分配端口并启动子进程
            it = ports.begin();
            for (int i = 0; i < cpu_cores && it != ports.end(); ++i) {
                auto end_it = std::next(it, std::min(ports_per_process, static_cast<int>(std::distance(it, ports.end()))));
                std::vector<int> process_ports(it, end_it);
                it = end_it;

                forkManager.createChildProcess([process_ports] {
                    processWorker(process_ports);
                });
            }
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