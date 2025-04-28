#pragma once

#include <iostream>
#include <unistd.h>
#include "../mstd/function.hpp"

namespace SOK{
    /// @brief 创建子进程
    /// @param func 子进程主要执行的函数
    void createChildProcess(mstd::Function<void()> func) {
        pid_t pid = fork();
        if (pid == 0) {
            func();
            exit(0);
        }
    }
}

