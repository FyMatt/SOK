#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <set>
#include <mutex>
#include <cstring>
#include <arpa/inet.h>
#include <fcntl.h>
#include "threadPool.hpp"
#include "../utils/ProtocolDispatcher.hpp"
#include "../utils/Config.hpp"
#include "../protocols/https.hpp"
#include <shared_mutex>

/// @brief 处理单个客户端连接，根据端口自动分发协议
inline bool handle_connection(int client_fd, int port, SSL_CTX* ssl_ctx) {
    return SOK::dispatch_protocol(client_fd, port, ssl_ctx);
}

/// @brief epoll监听客户端连接以及监听请求，主事件循环
/// @param epoll_fd epoll文件描述符
/// @param server_fds 监听的服务器文件描述符列表
inline void epoll_worker(int epoll_fd, std::vector<int> &server_fds, SSL_CTX* ssl_ctx) {
    struct epoll_event events[SOK::Config::instance().root().getValue<int>("per_process_max_events")];
    SOK_LOG_INFO("Epoll worker started on process " + std::to_string(getpid()) + "\t max thread count: " + std::to_string(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")));
    mstd::ThreadPool thread_pool(SOK::Config::instance().root().getValue<int>("per_process_max_thread_count")); // 创建线程池
    static std::map<int, int> client_map_port;
    static std::set<int> working_fds;
    static std::mutex client_map_port_mtx;
    static std::mutex working_fds_mtx;
    while (true) {
        int event_count = epoll_wait(epoll_fd, events, SOK::Config::instance().root().getValue<int>("per_process_max_events"), -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                // 新连接
                if (std::find(server_fds.begin(), server_fds.end(), client_fd) != server_fds.end()) {
                    sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int new_client_fd = accept(client_fd, (sockaddr*)&client_addr, &client_len);
                    if (new_client_fd != -1) {
                        int flags = fcntl(new_client_fd, F_GETFL, 0);
                        if (flags != -1) {
                            fcntl(new_client_fd, F_SETFL, flags | O_NONBLOCK);
                        }
                        char client_ip[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
                        int port = SOK::FdPortRegistry::instance().getPort(client_fd);
                        {
                            std::lock_guard<std::mutex> lock(client_map_port_mtx);
                            client_map_port[new_client_fd] = port;
                        }
                        epoll_event client_event{};
                        client_event.events = EPOLLIN;
                        client_event.data.fd = new_client_fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &client_event);
                    }
                } else {
                    bool skip = false;
                    {
                        std::lock_guard<std::mutex> lock1(working_fds_mtx);
                        if (working_fds.count(client_fd)) {
                            skip = true;
                        } else {
                            working_fds.insert(client_fd);
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock2(client_map_port_mtx);
                        if (client_map_port.find(client_fd) == client_map_port.end()) {
                            std::lock_guard<std::mutex> lock3(working_fds_mtx);
                            working_fds.erase(client_fd);
                            continue;
                        }
                    }
                    if (skip) continue;
                    int port = -1;
                    {
                        std::lock_guard<std::mutex> lock(client_map_port_mtx);
                        auto it = client_map_port.find(client_fd);
                        if (it != client_map_port.end()) {
                            port = it->second;
                        } else {
                            std::lock_guard<std::mutex> lock2(working_fds_mtx);
                            working_fds.erase(client_fd);
                            continue;
                        }
                    }
                    thread_pool.enqueue([client_fd, port, epoll_fd, ssl_ctx] {
                        try {
                            bool keep_alive = handle_connection(client_fd, port, ssl_ctx);
                            if (!keep_alive) {
                                // 关闭 fd 前同步清理 SSL*
                                auto& ssl_map = SOK::https_util::ssl_map;
                                {
                                    std::lock_guard<std::mutex> lock(SOK::https_util::ssl_map_mtx);
                                    auto it = ssl_map.find(client_fd);
                                    if (it != ssl_map.end()) {
                                        SSL_shutdown(it->second);
                                        SSL_free(it->second);
                                        ssl_map.erase(it);
                                    }
                                }
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                                close(client_fd);
                                {
                                    std::lock_guard<std::mutex> lock(client_map_port_mtx);
                                    client_map_port.erase(client_fd);
                                }
                            }
                        } catch(const std::exception& e) {
                            SOK_LOG_ERROR("Exception in thread: " + std::string(e.what()) + " for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(port));
                            auto& ssl_map = SOK::https_util::ssl_map;
                            {
                                std::lock_guard<std::mutex> lock(SOK::https_util::ssl_map_mtx);
                                auto it = ssl_map.find(client_fd);
                                if (it != ssl_map.end()) {
                                    SSL_shutdown(it->second);
                                    SSL_free(it->second);
                                    ssl_map.erase(it);
                                }
                            }
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            close(client_fd);
                            {
                                std::lock_guard<std::mutex> lock(client_map_port_mtx);
                                client_map_port.erase(client_fd);
                            }
                        } catch(...) {
                            SOK_LOG_ERROR("Unknown exception in thread for fd: " + std::to_string(client_fd) + " on port: " + std::to_string(port));
                            auto& ssl_map = SOK::https_util::ssl_map;
                            {
                                std::lock_guard<std::mutex> lock(SOK::https_util::ssl_map_mtx);
                                auto it = ssl_map.find(client_fd);
                                if (it != ssl_map.end()) {
                                    SSL_shutdown(it->second);
                                    SSL_free(it->second);
                                    ssl_map.erase(it);
                                }
                            }
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                            close(client_fd);
                            {
                                std::lock_guard<std::mutex> lock(client_map_port_mtx);
                                client_map_port.erase(client_fd);
                            }
                        }
                        {
                            std::lock_guard<std::mutex> lock(working_fds_mtx);
                            working_fds.erase(client_fd);
                        }
                    });  
                }
            }
        }
    }
}
