#include <iostream>
#include <unistd.h>
#include <sys/wait.h>

namespace SOK{
    void createChildProcess(void (*childFunction)()) {
        pid_t pid = fork();
        if (pid < 0) {
            std::cerr << "Fork failed" << std::endl;
            exit(1);
        } else if (pid == 0) {
            // Child process
            childFunction();
            exit(0);
        } else {
            // Parent process
            wait(NULL); // Wait for child process to finish
        }
    }
}

