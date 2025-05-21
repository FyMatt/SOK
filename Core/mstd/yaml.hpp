#pragma once

#include <string>
#include <map>
#include <any>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace mstd {

class YamlReader {
public:
    // 构造函数，接受文件路径或父对象的数据
    explicit YamlReader(const std::string& filePath) {
        parseFile(filePath);
    }

    explicit YamlReader(const std::map<std::string, std::any>& objectData) : data(objectData) {}

    // 获取值的方法，支持模板化返回类型
    template <typename T>
    T getValue(const std::string& key) const {
        auto it = data.find(key);
        if (it == data.end()) {
            throw std::runtime_error("Key not found: " + key);
        }

        // 检查类型是否匹配
        if (it->second.type() != typeid(T)) {
            throw std::runtime_error("Type mismatch for key: " + key);
        }

        if constexpr (std::is_same_v<T, std::string>) {
            std::string value = std::any_cast<std::string>(it->second);
            if (!value.empty() && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
            }
            return value;
        } else {
            return std::any_cast<T>(it->second);
        }
    }

    // 获取嵌套对象的方法
    YamlReader getObject(const std::string& key) const {
        auto it = data.find(key);
        if (it == data.end() || it->second.type() != typeid(std::map<std::string, std::any>)) {
            throw std::runtime_error("Object not found or invalid: " + key);
        }
        return YamlReader(std::any_cast<const std::map<std::string, std::any>&>(it->second));
    }

private:
    std::map<std::string, std::any> data;

    // 解析文件
    void parseFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filePath);
        }

        std::string line;
        std::vector<std::map<std::string, std::any>*> stack;
        stack.push_back(&data); // 根层级
        int previousIndent = 0;

        while (std::getline(file, line)) {
            // 计算当前行的缩进
            int currentIndent = countIndent(line);

            // 去除前后的空格
            trim(line);

            // 跳过空行和注释
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // 如果缩进减少，退出嵌套层级
            while (currentIndent < previousIndent) {
                if (stack.size() > 1) {
                    stack.pop_back();
                }
                previousIndent -= 2; // 假设每个缩进级别为2个空格
            }

            // 找到键值分隔符
            size_t delimiterPos = line.find(":");
            if (delimiterPos == std::string::npos) {
                throw std::runtime_error("Invalid line: " + line);
            }

            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            // 去除键和值的空格
            trim(key);
            trim(value);

            // 如果值为空，表示这是一个嵌套对象
            if (value.empty()) {
                auto& currentMap = *stack.back();
                currentMap[key] = std::map<std::string, std::any>();
                stack.push_back(&std::any_cast<std::map<std::string, std::any>&>(currentMap[key]));
            } else {
                // 根据值的内容动态确定类型
                if (value == "true" || value == "false") {
                    stack.back()->emplace(key, value == "true");
                } else if (value.find_first_not_of("0123456789") == std::string::npos) {
                    stack.back()->emplace(key, std::stoi(value));
                } else if (value.find_first_not_of("0123456789.") == std::string::npos) {
                    stack.back()->emplace(key, std::stod(value));
                } else {
                    stack.back()->emplace(key, value);
                }
            }

            // 更新缩进
            previousIndent = currentIndent;
        }
    }

    // 计算缩进级别
    int countIndent(const std::string& line) const {
        int count = 0;
        for (char c : line) {
            if (c == ' ') {
                count++;
            } else {
                break;
            }
        }
        return count;
    }

    // 去除字符串前后的空格
    void trim(std::string& str) const {
        size_t first = str.find_first_not_of(" \t\n\r");
        size_t last = str.find_last_not_of(" \t\n\r");
        if (first == std::string::npos || last == std::string::npos) {
            str.clear();
        } else {
            str = str.substr(first, last - first + 1);
        }
    }
};

}