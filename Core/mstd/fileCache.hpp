#pragma once

#include <string>
#include <list>
#include <unordered_map>
#include <shared_mutex>
#include <fstream>
#include "vector.hpp"
#include <algorithm>
#include <optional>
#include <sys/stat.h>

namespace mstd {

class FileCache {
public:
    struct CachedFile {
        std::vector<char> content; // 文件内容
        std::string mime_type; // 文件的MIME类型
        time_t last_modified; // 文件的最后修改时间
        size_t file_size; // 文件大小
        std::list<std::string>::iterator lru_it; // LRU列表中的迭代器
    };
    
    //  显示构造函数
    explicit FileCache(size_t max_size = 1024 * 1024 * 100) // 默认最大缓存大小为100MB
        : max_size_(max_size), current_size_(0), cache_hits_(0), cache_misses_(0) {}

    //  获取文件内容和类型, 先判断文件是否已经更新
    std::optional<std::pair<std::vector<char>, std::string>> get(const std::string& file_path) {
        std::unique_lock lock(mutex_); // 多线程下所有操作都用独占锁
        auto it = cache_.find(file_path);
        if (it != cache_.end()) {
            // 检查文件是否已修改
            if (is_file_modified(file_path, it->second.last_modified)) {
                // 文件已更新，移除旧缓存
                current_size_ -= it->second.file_size;
                lru_list_.erase(it->second.lru_it);
                cache_.erase(it);
                cache_misses_++;
            } else {
                // 更新LRU位置
                lru_list_.erase(it->second.lru_it);
                lru_list_.push_front(file_path);
                it->second.lru_it = lru_list_.begin();
                cache_hits_++;
                return std::make_optional(std::make_pair(it->second.content, it->second.mime_type));
            }
        } else {
            cache_misses_++;
        }

        // 加载新文件到缓存
        CachedFile new_file;
        if (!load_file(file_path, new_file)) {
            return std::nullopt;
        }

        // 添加新条目
        lru_list_.push_front(file_path);
        new_file.lru_it = lru_list_.begin();
        cache_.insert_or_assign(file_path, new_file);
        current_size_ += new_file.file_size;

        // 清理过期缓存
        while (current_size_ > max_size_) {
            evict();
        }

        return std::make_optional(std::make_pair(new_file.content, new_file.mime_type));
    }

    void set_max_size(size_t max_size) {
        std::unique_lock lock(mutex_); // 独占锁用于写操作
        max_size_ = max_size;
        while (current_size_ > max_size_) {
            evict();
        }
    }

    // 获取缓存命中次数
    size_t get_cache_hits() const {
        std::shared_lock lock(mutex_);
        return cache_hits_;
    }

    // 获取缓存未命中次数
    size_t get_cache_misses() const {
        std::shared_lock lock(mutex_);
        return cache_misses_;
    }

private:
    time_t get_file_last_write_time(const std::string& file_path) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            return 0; // 获取文件信息失败
        }
        return file_stat.st_mtime; // 获取文件的最后修改时间
    }

    size_t get_file_size(const std::string& file_path) {
        struct stat file_stat;
        if (stat(file_path.c_str(), &file_stat) != 0) {
            return 0; // 获取文件信息失败
        }
        return file_stat.st_size; // 获取文件大小
    }

    //  查看文件是否已经修改
    bool is_file_modified(const std::string& file_path, time_t cached_time) {
        return get_file_last_write_time(file_path) > cached_time; // 检查文件是否已修改
    }

    //  加载文件内容
    bool load_file(const std::string& file_path, CachedFile& result) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate); // 以二进制方式打开文件，并定位到文件末尾
        if (!file.is_open()) return false;

        auto file_size = get_file_size(file_path); // 获取文件大小
        if (file_size == 0) return false;

        result.content.resize(file_size); // 调整内容缓冲区大小
        file.seekg(0); // 定位到文件开头
        file.read(result.content.data(), result.content.size()); // 读取文件内容
        
        result.file_size = file_size;
        result.last_modified = get_file_last_write_time(file_path); // 获取文件的最后修改时间
        result.mime_type = get_mime_type(file_path); // 获取文件的MIME类型
        return true;
    }

    //  从缓存中移除最近最少使用的文件 (LRU)
    void evict() {
        // 这里假定外层已加锁
        if (lru_list_.empty()) return;

        const std::string& lru_file = lru_list_.back(); // 获取LRU列表中的最后一个文件
        auto it = cache_.find(lru_file);
        if (it != cache_.end()) {
            current_size_ -= it->second.file_size; // 更新当前缓存大小
            cache_.erase(it); // 从缓存中移除文件
        }
        lru_list_.pop_back(); // 从LRU列表中移除文件
    }

    std::string get_mime_type(const std::string& file_path) {
        size_t dot_pos = file_path.find_last_of('.');
        if (dot_pos == std::string::npos) return "application/octet-stream";

        std::string ext = file_path.substr(dot_pos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); // 转换扩展名为小写

        static const std::unordered_map<std::string, std::string> mime_types = {
            {"html", "text/html; charset=utf-8"},
            {"htm",  "text/html; charset=utf-8"},
            {"css",  "text/css; charset=utf-8"},
            {"js",   "application/javascript; charset=utf-8"},
            {"json", "application/json; charset=utf-8"},
            {"png",  "image/png"},
            {"jpg",  "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"gif",  "image/gif"},
            {"svg",  "image/svg+xml"},
            {"txt",  "text/plain; charset=utf-8"},
            {"ico",  "image/x-icon"}
        };

        auto it = mime_types.find(ext);
        return it != mime_types.end() ? it->second : "application/octet-stream"; // 返回对应的MIME类型
    }

    size_t max_size_; // 最大缓存大小
    size_t current_size_; // 当前缓存大小
    std::list<std::string> lru_list_; // LRU列表    Least Recently Used，最近最少使用
    std::unordered_map<std::string, CachedFile> cache_; // 文件缓存
    mutable std::shared_mutex mutex_; // 读写锁
    size_t cache_hits_; // 缓存命中次数
    size_t cache_misses_; // 缓存未命中次数
};

}
