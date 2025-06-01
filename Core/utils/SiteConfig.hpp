#pragma once
#include <string>

namespace SOK {
namespace utils {

class ISiteConfig {
public:
    virtual ~ISiteConfig() = default;
    virtual std::string getRootDir(int port) const = 0;
    virtual int getPort() const = 0;
};

// 示例实现，可根据实际需求扩展
class SimpleSiteConfig : public ISiteConfig {
    std::string rootDir;
    int port;
public:
    SimpleSiteConfig(const std::string& root, int p) : rootDir(root), port(p) {}
    std::string getRootDir(int p) const override {
        if (p == port) return rootDir;
        return "./";
    }
    int getPort() const override { return port; }
};

}
}
