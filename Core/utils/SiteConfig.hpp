#pragma once
#include <string>
#include <vector>
#include <stdexcept>
#include "../mstd/yaml.hpp"
#include "../utils/Config.hpp"

namespace SOK {
namespace utils {

class ISiteConfig {
public:
    virtual ~ISiteConfig() = default;
    virtual std::string getSiteName() const = 0;
    virtual std::string getRootDir() const = 0;
    virtual int getPort() const = 0;
};


class SiteInfo : public ISiteConfig {
public:
    /// @brief 
    /// @param siteName 站点名称
    /// @param root 站点根路径
    /// @param p 站点监听端口
    SiteInfo(int p)
        : site_config(SOK::Config::instance().root())
    {
        auto servers = site_config.getArray("servers");
        for (const auto& server : servers) {
            if (p == server.getValue<int>("port")) {
                name = server.getValue<std::string>("name");
                rootDir = server.getValue<std::string>("root");
                port = p;
                site_config = server;
                break;
            }
        }
    }
    
    /// @brief 获取站点根目录
    /// @return 
    std::string getRootDir() const override { return rootDir; }

    /// @brief 获取站点监听的端口
    /// @return 
    int getPort() const override { return port; }

    /// @brief 获取站点名称
    /// @return 
    std::string getSiteName() const override {
        if (name.empty()) {
            throw std::runtime_error("Site name is not set");
        }
        return name;
    }

    /// @brief 获取站点配置
    /// @return
    const mstd::YamlReader& getSiteConfig() const {
        return site_config;
    }
private:
    /// @brief 站点名称
    std::string name;

    /// @brief 站点根目录
    std::string rootDir;

    /// @brief 站点监听端口
    int port;

    /// @brief 站点配置
    mstd::YamlReader site_config;
};

}
}
