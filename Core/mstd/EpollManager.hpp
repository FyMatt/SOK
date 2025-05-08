#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <mutex>
#include <cstring>
#include "ThreadPool.hpp"

const int MAX_EVENTS = 10;
const int READ_SIZE = 1024;

std::mutex mtx;


/// @brief 处理epoll工作线程监听到的请求
/// @param client_fd 要处理请求的文件描述符
void handle_connection(int client_fd) {
    char buffer[READ_SIZE];
    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, READ_SIZE);
        if (bytes_read <= 0) {
            close(client_fd);
            break;
        }
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Received: " << std::string(buffer, bytes_read) << std::endl;
        std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
        write(client_fd, response.c_str(), response.size());
    }
}


/// @brief epoll监听客户端连接以及监听请求
/// @param epoll_fd epoll文件描述符
/// @param server_fd 服务端文件描述符
void epoll_worker(int epoll_fd, int server_fd) {
    struct epoll_event events[MAX_EVENTS];
    // mstd::ThreadPool thread_pool(std::thread::hardware_concurrency()); // 创建线程池
    mstd::ThreadPool thread_pool(2); // 创建线程池

    while (true) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;

                // 如果是服务器套接字，接受新连接
                if (client_fd == server_fd) {
                    int new_client_fd = accept(server_fd, nullptr, nullptr);
                    if (new_client_fd == -1) {
                        std::cerr << "Failed to accept new connection" << std::endl;
                        continue;
                    }

                    epoll_event client_event{};
                    client_event.events = EPOLLIN;
                    client_event.data.fd = new_client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &client_event);
                } else {
                    // 如果是客户端套接字，处理请求
                    thread_pool.enqueue([client_fd] {
                        handle_connection(client_fd);
                    });
                }
            }
        }
    }
}
