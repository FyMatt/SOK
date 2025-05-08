#pragma once

#include <functional>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include <signal.h>
#include "../mstd/function.hpp"

namespace SOK {
    class ForkManager {
    public:
        /// @brief 创建子进程并执行指定函数
        /// @param func 子进程主要执行的函数
        void createChildProcess(mstd::Function<void()> func) {
            pid_t pid = fork();
            if (pid == 0) {
                // 子进程
                func();
                exit(0);
            } else if (pid > 0) {
                // 父进程
                child_pids.push_back(pid);
            } else {
                perror("Failed to fork");
            }
        }

        /// @brief 终止所有子进程
        void terminateAll() {
            for (pid_t pid : child_pids) {
                kill(pid, SIGTERM);
                waitpid(pid, nullptr, 0);
            }
            child_pids.clear();
        }

        /// @brief 检查子进程状态
        void monitorChildren() {
            for (auto it = child_pids.begin(); it != child_pids.end();) {
                if (waitpid(*it, nullptr, WNOHANG) > 0) {
                    it = child_pids.erase(it);
                } else {
                    ++it;
                }
            }
        }

    private:
        std::vector<pid_t> child_pids; // 存储子进程的 PID
    };
}