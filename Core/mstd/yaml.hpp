#pragma once

#include <string>
#include <map>
#include <any>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <iostream>

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

    // 获取数组类型的方法
    std::vector<YamlReader> getArray(const std::string& key) const {
        auto it = data.find(key);
        if (it == data.end()) {
            throw std::runtime_error("Array not found: " + key);
        }
        // 支持 vector<map> 或 vector<any>（兼容更广泛的YAML数组）
        if (it->second.type() == typeid(std::vector<std::map<std::string, std::any>>)) {
            const auto& vec = std::any_cast<const std::vector<std::map<std::string, std::any>>&>(it->second);
            std::vector<YamlReader> result;
            for (const auto& obj : vec) {
                result.emplace_back(obj);
            }
            return result;
        } else if (it->second.type() == typeid(std::vector<std::any>)) {
            const auto& vec = std::any_cast<const std::vector<std::any>&>(it->second);
            std::vector<YamlReader> result;
            for (const auto& obj : vec) {
                if (obj.type() == typeid(std::map<std::string, std::any>)) {
                    result.emplace_back(std::any_cast<const std::map<std::string, std::any>&>(obj));
                } else {
                    // 支持基础类型数组
                    std::map<std::string, std::any> tmp;
                    tmp["value"] = obj;
                    result.emplace_back(tmp);
                }
            }
            return result;
        } else {
            throw std::runtime_error("Array type not supported for key: " + key);
        }
    }

private:
    std::map<std::string, std::any> data;

    // 解析文件
    void parseFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "[YamlReader] Failed to open file: " << filePath << std::endl;
            return; // 不抛异常，直接返回，data保持为空
        }

        std::string line;
        std::vector<std::map<std::string, std::any>*> stack;
        stack.push_back(&data); // 根层级
        int previousIndent = 0;
        std::vector<std::vector<std::map<std::string, std::any>>*> array_stack;
        std::vector<int> array_indent_stack; // 记录每个数组的起始缩进

        while (std::getline(file, line)) {
            int currentIndent = countIndent(line);
            trim(line);
            if (line.empty() || line[0] == '#') continue;

            // 处理缩进，退出嵌套层级
            while (currentIndent < previousIndent) {
                // 只有当当前缩进小于数组起始缩进时才pop
                while (!array_indent_stack.empty() && currentIndent < array_indent_stack.back()) {
                    array_stack.pop_back();
                    array_indent_stack.pop_back();
                }
                if (stack.size() > 1) stack.pop_back();
                previousIndent -= 2;
            }

            if (line[0] == '-') {
                // YAML数组元素
                std::string item = line.substr(1);
                trim(item);
                if (array_stack.empty()) {
                    throw std::runtime_error("YAML array '-' without a parent key");
                }
                std::map<std::string, std::any> obj;
                array_stack.back()->push_back(obj);
                stack.push_back(&array_stack.back()->back());
                if (!item.empty()) {
                    size_t delimiterPos = item.find(":");
                    if (delimiterPos == std::string::npos) throw std::runtime_error("Invalid array item: " + item);
                    std::string key = item.substr(0, delimiterPos);
                    std::string value = item.substr(delimiterPos + 1);
                    trim(key); trim(value);
                    if (value.empty()) {
                        (*stack.back())[key] = std::map<std::string, std::any>();
                        stack.push_back(&std::any_cast<std::map<std::string, std::any>&>((*stack.back())[key]));
                    } else {
                        (*stack.back())[key] = value;
                    }
                }
                previousIndent = currentIndent;
                continue;
            }

            // 普通key: value
            size_t delimiterPos = line.find(":");
            if (delimiterPos == std::string::npos) throw std::runtime_error("Invalid line: " + line);
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            trim(key); trim(value);
            if (value.empty()) {
                // 判断下一行是否是数组（跳过注释和空行）
                std::streampos pos = file.tellg();
                std::string nextLine;
                bool isArray = false;
                while (std::getline(file, nextLine)) {
                    trim(nextLine);
                    if (nextLine.empty() || nextLine[0] == '#') continue;
                    if (nextLine[0] == '-') isArray = true;
                    break;
                }
                file.clear();
                file.seekg(pos);
                if (isArray) {
                    (*stack.back())[key] = std::vector<std::map<std::string, std::any>>();
                    array_stack.push_back(&std::any_cast<std::vector<std::map<std::string, std::any>>&>((*stack.back())[key]));
                    array_indent_stack.push_back(currentIndent + 2); // 记录数组的起始缩进
                } else {
                    (*stack.back())[key] = std::map<std::string, std::any>();
                    stack.push_back(&std::any_cast<std::map<std::string, std::any>&>((*stack.back())[key]));
                }
            } else {
                // 支持基础类型数组（如 arr: [1,2,3]）
                if (value.front() == '[' && value.back() == ']') {
                    std::vector<std::any> arr;
                    std::string inner = value.substr(1, value.size() - 2);
                    std::stringstream ss(inner);
                    std::string elem;
                    while (std::getline(ss, elem, ',')) {
                        trim(elem);
                        if (!elem.empty()) arr.push_back(elem);
                    }
                    (*stack.back())[key] = arr;
                } else {
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
            }
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