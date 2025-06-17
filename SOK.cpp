#include <iostream>
#include <numeric>
#include <atomic>
#include <signal.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <sys/wait.h>
#include "Core/utils/ForkManager.hpp"
#include "Core/utils/SocketUtils.hpp"
#include "Core/mstd/EpollManager.hpp"
#include "Core/utils/Logger.hpp"
#include "Core/utils/Config.hpp"

std::atomic<bool> running(true);


/// @brief 子进程信号处理函数，用于处理子进程退出事件
/// @param signo 
void sigchld_handler(int signo) {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            SOK_LOG_ERROR("Reaped child process pid=" + std::to_string(pid) +
                ", exited with code " + std::to_string(WEXITSTATUS(status)));
        } else if (WIFSIGNALED(status)) {
            SOK_LOG_ERROR("Reaped child process pid=" + std::to_string(pid) +
                ", killed by signal " + std::to_string(WTERMSIG(status)));
        }
    }
}


/// @brief  信号处理函数，用于处理SIGINT信号（Ctrl+C）
/// @param signum 
void signalHandler(int signum) {
    if (signum == SIGINT) {
        running.store(false);
    }
}

/// @brief 每个进程监听一组端口，主事件循环
/// @param ports 要监听的端口列表
void processWorker(const std::vector<int>& ports) {
    try {
        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("Failed to create epoll instance");
            exit(EXIT_FAILURE);
        }

        // 全局只创建一次 SSL_CTX
        static SSL_CTX* ssl_ctx = SOK::https_util::create_ssl_ctx("server.crt", "server.key");

        std::vector<int> server_fds;
        for (int port : ports) {
            int server_fd = SOK::setup_server(port);
            SOK::FdPortRegistry::instance().addFdPort(server_fd, port);
            epoll_event event;
            event.events = EPOLLIN;
            event.data.fd = server_fd;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
                perror("Failed to add server_fd to epoll");
                close(server_fd);
                continue;
            }
            server_fds.push_back(server_fd);
            SOK_LOG_INFO("Process " + std::to_string(getpid()) + " listening on port " + std::to_string(port));
        }
        epoll_worker(epoll_fd, server_fds, ssl_ctx);

        for (int fd : server_fds) {
            close(fd);
        }
        close(epoll_fd);
    } catch (const std::exception& ex) {
        SOK_LOG_ERROR(std::string("子进程异常退出: ") + ex.what());
        exit(EXIT_FAILURE);
    } catch (...) {
        SOK_LOG_ERROR("子进程发生未知异常，退出");
        exit(EXIT_FAILURE);
    }
}

int main() {
    // 忽略SIGPIPE，防止写已关闭socket时进程被杀死
    signal(SIGPIPE, SIG_IGN);
    // 注册SIGCHLD信号处理器
    signal(SIGCHLD, sigchld_handler);
    // 注册SIGINT信号处理器
    signal(SIGINT, signalHandler);

    SOK::FdPortRegistry::instance();
    SOK::Logger::instance().set_logfile("server.log");
    SOK_LOG_INFO("Server started...");

    SOK::Config::instance().load("config.yaml");

    std::vector<int> ports;
    auto root = SOK::Config::instance().root();
    auto servers = root.getArray("servers");
    for (const auto& server : servers) {
        int port = server.getValue<int>("port");
        ports.push_back(port);
    }

    SOK_LOG_INFO("Loaded configuration...");
    SOK_LOG_INFO("Successfully initialized SOK server.");

    SOK::ForkManager forkManager;

    int cpu_cores;
    if(root.getValue<int>("cpu_cores") > 0) {
        cpu_cores = root.getValue<int>("cpu_cores");
    } else {
        cpu_cores = std::thread::hardware_concurrency();
    }
    SOK_LOG_INFO("Detected " + std::to_string(cpu_cores) + " CPU cores.");

    SOK_LOG_INFO("Listening on ports: " +
        std::accumulate(
            std::next(ports.begin()), ports.end(), std::to_string(ports[0]),
            [](std::string a, int b) { return std::move(a) + " " + std::to_string(b); }
        )
    );

    // 启动每个子进程都监听所有端口
    for (int i = 0; i < cpu_cores; ++i) {
        forkManager.createChildProcess([ports] {
            processWorker(ports); // 每个进程都监听所有端口
        });
    }

    while (running.load()) {
        std::string command;
        std::cout << "Enter command (restart/exit): ";
        std::cin >> command;

        if (command == "restart") {
            SOK_LOG_INFO("Restarting child processes...");
            forkManager.terminateAll();

            SOK::Logger::instance().set_logfile("server.log");
            SOK::Config::instance().load("config.yaml");

            for (int i = 0; i < cpu_cores; ++i) {
                forkManager.createChildProcess([ports] {
                    processWorker(ports); // 每个进程都监听所有端口
                });
            }

        } else if (command == "exit") {
            running.store(false);
        }
        forkManager.monitorChildren();
    }

    forkManager.terminateAll();
    
    return 0;
}