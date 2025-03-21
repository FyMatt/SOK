#pragma once
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <memory>
#include <string>
#include "iterator.hpp"

namespace mstd {

class string {
public:
    using iterator = Iterator<char>;
    using const_iterator = Iterator<const char>;

    string() : data_(nullptr), size_(0), capacity_(0) {}
    
    string(const char* s) {
        size_ = std::strlen(s);
        capacity_ = size_;
        data_ = std::make_unique<char[]>(capacity_ + 1);
        std::memcpy(data_.get(), s, size_ + 1);
    }

    // 拷贝构造函数
    string(const string& other) : size_(other.size_), capacity_(other.capacity_) {
        data_ = std::make_unique<char[]>(capacity_ + 1);
        std::memcpy(data_.get(), other.data_.get(), size_ + 1);
    }

    // 移动构造函数
    string(string&& other) noexcept : data_(std::move(other.data_)), size_(other.size_), capacity_(other.capacity_) {
        other.size_ = 0;
        other.capacity_ = 0;
        other.data_ = nullptr;
    }

    // 重载赋值符号
    string& operator=(const string& other) noexcept {
        if (this != &other) {
            data_ = std::make_unique<char[]>(other.capacity_ + 1);
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_.get(), other.data_.get(), size_ + 1);
        }
        return *this;
    }

    // 重载赋值符号
    string& operator=(string&& other) noexcept {
        if (this != &other) {
            data_ = std::move(other.data_);
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.size_ = 0;
            other.capacity_ = 0;
            other.data_ = nullptr;
        }
        return *this;
    }

    /// @brief 返回字符数组
    /// @return 
    inline const char* c_str() const {
        return data_.get();
    }

    /// @brief 返回字符串长度
    /// @return 
    inline std::size_t size() const {
        return size_;
    }

    /// @brief 返回字符串内存大小
    /// @return 
    inline std::size_t capacity() const {
        return capacity_;
    }

    /// @brief 扩大字符串的内存大小
    /// @param new_capacity 字符串新的内存大小
    void reserve(std::size_t new_capacity) {
        if (new_capacity > capacity_) {
            auto new_data = std::make_unique<char[]>(new_capacity + 1);
            if (data_) {
                std::memcpy(new_data.get(), data_.get(), size_ + 1);
            }
            data_ = std::move(new_data);
            capacity_ = new_capacity;
        }
    }

    /// @brief 重新设置字符串长度
    /// @param new_size 字符串新的长度
    void resize(std::size_t new_size) {
        if (new_size > capacity_) {
            reserve(new_size);
        }
        size_ = new_size;
        data_[size_] = '\0';
    }

    char& operator[](std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

    const char& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("Index out of range");
        }
        return data_[index];
    }

    iterator begin() {
        return iterator(data_.get());
    }

    const_iterator begin() const {
        return const_iterator(data_.get());
    }

    iterator end() {
        return iterator(data_.get() + size_);
    }

    const_iterator end() const {
        return const_iterator(data_.get() + size_);
    }

    string substr(std::size_t pos, std::size_t len) const {
        if (pos > size_) {
            throw std::out_of_range("Position out of range");
        }
        len = std::min(len, size_ - pos);
        string result;
        result.resize(len);
        std::memcpy(result.data_.get(), data_.get() + pos, len);
        result.data_[len] = '\0';
        return result;
    }

    // 添加转换为std::string的函数
    std::string to_std_string() const {
        return std::string(data_.get());
    }

    // 添加隐式转换运算符
    operator std::string() const {
        return to_std_string();
    }

    // 重载 << 符号, 为了在std::cout<<中使用
    friend std::ostream& operator<<(std::ostream& os, const string& str) {
        os << str.c_str();
        return os;
    }

private:
    std::unique_ptr<char[]> data_;
    std::size_t size_;
    std::size_t capacity_;
};

}