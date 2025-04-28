#include "Core/SOK_Header.hpp"

void childProcess() {
    std::cout << "This is child process pid: " << getpid() << std::endl;
}

int main() {
    // 在此插入测试代码
    SOK::createChildProcess(childProcess);
    SOK::createChildProcess(childProcess);
    

    return 0;
}
