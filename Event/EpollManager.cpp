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

int main() {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll file descriptor: " << strerror(errno) << std::endl;
        return 1;
    }

    // Add your listening socket to the epoll instance here
    // Example:
    // int listen_fd = ...;
    // struct epoll_event event;
    // event.events = EPOLLIN;
    // event.data.fd = listen_fd;
    // if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) == -1) {
    //     std::cerr << "Failed to add file descriptor to epoll: " << strerror(errno) << std::endl;
    //     close(epoll_fd);
    //     return 1;
    // }

    std::thread(epoll_worker, epoll_fd).detach();

    // Main thread can perform other tasks or wait for termination
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    close(epoll_fd);
    return 0;
}