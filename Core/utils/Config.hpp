#pragma once
#include <string>
#include <mutex>
#include "../mstd/yaml.hpp"

namespace SOK {
class Config {
public:
    static Config& instance() {
        static Config inst;
        return inst;
    }
    void load(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        yaml_ = mstd::YamlReader(filename);
    }
    // 直接暴露YamlReader的接口
    template<typename T>
    T getValue(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return yaml_.getValue<T>(key);
    }
    mstd::YamlReader getObject(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return yaml_.getObject(key);
    }
    const mstd::YamlReader& root() const { return yaml_; }
private:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;
    mstd::YamlReader yaml_{std::map<std::string, std::any>()}; // 默认空map
    mutable std::mutex mutex_;
};
}
