#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <cstring>
#include <arpa/inet.h>
#include "threadPool.hpp"
#include "../utils/ProtocolDispatcher.hpp"
#include "../utils/Config.hpp"

const int MAX_EVENTS = 1024;

/// @brief 处理epoll工作线程监听到的请求
/// @param client_fd 要处理请求的文件描述符
void handle_connection(int client_fd, int port) {
    while (true) {
        SOK::dispatch_protocol(client_fd, port);
    }
}

/// @brief epoll监听客户端连接以及监听请求
/// @param epoll_fd epoll文件描述符
/// @param unused 未使用的参数
void epoll_worker(int epoll_fd) {
    struct epoll_event events[MAX_EVENTS];
    // SOK::Logger::instance().info("Epoll worker started on process " + std::to_string(getpid()) + "\t max thread count: " + std::to_string(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")));
    mstd::ThreadPool thread_pool(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")); // 创建线程池
    // mstd::ThreadPool thread_pool(2); // 创建线程池

    while (true) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                int port = SOK::FdPortRegistry::instance().getPort(client_fd);
                if(port == -1) {
                    // std::cerr << "client_fd: " << client_fd << "\tFailed to get port for client_fd: " << client_fd << std::endl;
                    continue; // 如果没有找到对应的端口，跳过这个事件
                }
                // SOK::Logger::instance().info("server_client_fd: " + std::to_string(client_fd));
                sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int new_client_fd = accept(client_fd, (sockaddr*)&client_addr, &client_len);
                if (new_client_fd != -1) {
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                    // std::cout << "New connection from: " << client_ip << " on process " << getpid() << std::endl;
                }
                if (new_client_fd == -1) {
                    // std::cerr << "Failed to accept new connection" << std::endl;
                    continue;
                }
                // SOK::Logger::instance().info("new_client_fd: " + std::to_string(new_client_fd));
                
                // SOK::Logger::instance().info("Rec http from port: " + std::to_string(port));
                // SOK::Logger::instance().info("new_client_fd: " + std::to_string(new_client_fd));
                epoll_event client_event{};
                client_event.events = EPOLLIN;
                client_event.data.fd = new_client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &client_event);

                // 将请求交给线程池处理
                thread_pool.enqueue([new_client_fd, port] {
                    handle_connection(new_client_fd, port);
                });
            }
        }
    }
}
