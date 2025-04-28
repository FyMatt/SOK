#pragma once

#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <cstring>

const int MAX_EVENTS = 10;
const int READ_SIZE = 1024;

std::mutex mtx;

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
    }
}

void epoll_worker(int epoll_fd) {
    struct epoll_event events[MAX_EVENTS];
    while (true) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].events & EPOLLIN) {
                int client_fd = events[i].data.fd;
                std::thread(handle_connection, client_fd).detach();
            }
        }
    }
}
