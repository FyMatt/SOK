#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <mutex>
#include <cstring>
#include <arpa/inet.h>
#include "threadPool.hpp"
#include "../utils/ProtocolDispatcher.hpp"
#include "../utils/Config.hpp"


// 返回是否需要继续监听（keep-alive）
bool handle_connection(int client_fd, int port) {
    return SOK::dispatch_protocol(client_fd, port);
}


/// @brief epoll监听客户端连接以及监听请求
/// @param epoll_fd epoll文件描述符
/// @param server_fds 监听的服务器文件描述符列表
void epoll_worker(int epoll_fd, std::vector<int> &server_fds) {
    struct epoll_event events[SOK::Config::instance().root().getValue<int>("per_process_max_events")];
    SOK_LOG_INFO("Epoll worker started on process " + std::to_string(getpid()) + "\t max thread count: " + std::to_string(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")));
    mstd::ThreadPool thread_pool(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")); // 创建线程池
    static std::map<int, int> client_map_port;
    static std::set<int> working_fds;
    static std::mutex working_fds_mtx;

    while (true) {
        int event_count = epoll_wait(epoll_fd, events, SOK::Config::instance().root().getValue<int>("per_process_max_events"), -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                if (std::find(server_fds.begin(), server_fds.end(), client_fd) != server_fds.end()) {
                    // 监听到新的客户端连接
                    sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int new_client_fd = accept(client_fd, (sockaddr*)&client_addr, &client_len);
                    if (new_client_fd != -1) {
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        
                        // 从记录的监听服务器的文件描述符中获取端口号
                        // 设置新连接的客户端文件描述符对应的端口号
                        int port = SOK::FdPortRegistry::instance().getPort(client_fd);
                        client_map_port[new_client_fd] = port;

                        epoll_event client_event{};
                        client_event.events = EPOLLIN;
                        client_event.data.fd = new_client_fd;

                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &client_event);
                        // std::cout << "New connection from: " << client_ip << " on process " << getpid() << std::endl;  
                    }
                }else{
                    // 处理已连接的客户端请求
                    {
                        std::lock_guard<std::mutex> lock(working_fds_mtx);
                        if (working_fds.count(client_fd) || client_map_port.find(client_fd) == client_map_port.end()) {
                            continue;
                        }
                        working_fds.insert(client_fd);
                    }
                    int port = client_map_port.find(client_fd)->second;
                    thread_pool.enqueue([client_fd, port, epoll_fd] {
                        std::ostringstream out;
                        out << std::this_thread::get_id();
                        SOK_LOG_INFO("线程: " + out.str() + " fd: " + std::to_string(client_fd) + 
                            "[" + std::to_string(port) + "]" + " 的请求数据");

                        bool keep_alive = handle_connection(client_fd, port);
                        if (!keep_alive) {
                            close(client_fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                        } else {
                            // 不关闭fd，不移除epoll，等待下次有数据再处理
                            {
                                std::lock_guard<std::mutex> lock(working_fds_mtx);
                            working_fds.erase(client_fd);
                            }
                        }
                    });  
                }
            }
        }
    }
}
