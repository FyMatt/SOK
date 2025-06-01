#include <iostream>
#include <numeric>
#include <atomic>
#include <signal.h>
#include <thread>
#include <vector>
#include <algorithm>
#include "Core/utils/ForkManager.hpp"
#include "Core/utils/SocketUtils.hpp"
#include "Core/mstd/EpollManager.hpp"
#include "Core/utils/Logger.hpp"
#include "Core/utils/Config.hpp"

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
        int server_fd = SOK::setup_server(port);
        SOK::FdPortRegistry::instance().addFdPort(server_fd, port);
        // SOK::Logger::instance().info("server_fd: " + std::to_string(server_fd));
        // if(SOK::FdPortRegistry::instance().addFdPort(server_fd, port)){
        //     SOK::Logger::instance().info("Added fd-port mapping: " + std::to_string(server_fd) + " -> " + std::to_string(port));
        // } else {
        //     SOK::Logger::instance().warn("Failed to add fd-port mapping for fd: " + std::to_string(server_fd) + ", port: " + std::to_string(port));
        //     close(server_fd);
        //     continue;
        // }
        // SOK::Logger::instance().info("port: " + std::to_string(SOK::FdPortRegistry::instance().getPort(server_fd)));
        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = server_fd;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            perror("Failed to add server_fd to epoll");
            close(server_fd);
            continue;
        }
        server_fds.push_back(server_fd);
        SOK::Logger::instance().info("Process " + std::to_string(getpid()) + " listening on port " + std::to_string(port));
    }
    epoll_worker(epoll_fd); // -1 表示不需要特定的 server_fd

    for (int fd : server_fds) {
        close(fd);
    }
    close(epoll_fd);
}

int main() {
    signal(SIGINT, signalHandler);

    // 初始化描述符和端口号映射记录
    SOK::FdPortRegistry::instance();

    // 初始化日志系统
    SOK::Logger::instance().set_logfile("server.log");
    SOK::Logger::instance().info("Server started...");
    
    // 加载配置文件
    SOK::Config::instance().load("config.yaml");

    // 获取配置中的端口列表
    std::vector<int> ports;

    // 测试yaml解析输出
    auto root = SOK::Config::instance().root();
    std::string ip = root.getValue<std::string>("ip");
    int proc_num = root.getValue<int>("cpu_cores");
    SOK::Logger::instance().info("Config IP: " + ip);
    SOK::Logger::instance().info("Config cpu_cores: " + std::to_string(proc_num));
    auto servers = SOK::Config::instance().root().getArray("servers");
    for (const auto& server : servers) {
        std::string name = server.getValue<std::string>("name");
        int port = server.getValue<int>("port");
        ports.push_back(port);
        std::string root_dir = server.getValue<std::string>("root");
        SOK::Logger::instance().info("Server: name=" + name + ", port=" + std::to_string(port) + ", root=" + root_dir);
        // 检查是否有 locations 属性
        try {
            auto locations = server.getArray("locations");
            for (const auto& loc : locations) {
                // location 可能是对象，也可能是嵌套对象
                try {
                    auto location_obj = loc.getObject("location");
                    std::string url = location_obj.getValue<std::string>("url");
                    std::string proxy_pass, response;
                    try { proxy_pass = location_obj.getValue<std::string>("proxy_pass"); } catch (...) {}
                    try { response = location_obj.getValue<std::string>("response"); } catch (...) {}
                    SOK::Logger::instance().info("  Location: url=" + url + (proxy_pass.empty() ? "" : (", proxy_pass=" + proxy_pass)) + (response.empty() ? "" : (", response=" + response)));
                } catch (...) {}
            }
        } catch (...) {
            // 没有locations属性，忽略
        }
    }

    SOK::Logger::instance().info("Loaded configuration...");
    
    SOK::Logger::instance().info("Successfully initialized SOK server.");


    SOK::ForkManager forkManager;

    // 获取 CPU 核心数
    int cpu_cores;
    if(root.getValue<int>("cpu_cores") > 0) {
        cpu_cores = root.getValue<int>("cpu_cores");
    } else {
        cpu_cores = std::thread::hardware_concurrency();
    }
    SOK::Logger::instance().info("Detected " + std::to_string(cpu_cores) + " CPU cores.");

    // 要监听的端口列表
    // std::vector<int> ports = {8081, 8082};
    SOK::Logger::instance().info("Listening on ports: " + 
        std::accumulate(
            std::next(ports.begin()), ports.end(), std::to_string(ports[0]),
            [](std::string a, int b) { return std::move(a) + " " + std::to_string(b); }
        )
    );

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
            SOK::Logger::instance().info("Restarting child processes...");
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