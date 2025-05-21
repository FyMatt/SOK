#pragma once

#include <stdexcept>
#include <initializer_list>
#include <memory>
#include <cmath>
#include "iterator.hpp"

namespace mstd {

template <typename T>
class vector {
private:
    std::unique_ptr<T[]> data;
    size_t capacity;
    size_t size;

    /// @brief 重定义大小
    /// @param new_capacity 新的内存大小
    void resize(size_t new_capacity) {
        std::unique_ptr<T[]> new_data = std::make_unique<T[]>(new_capacity);
        for (size_t i = 0; i < size; ++i) {
            new_data[i] = std::move(data[i]);
        }
        data = std::move(new_data);
        capacity = new_capacity;
    }

public:
    vector() : data(nullptr), capacity(0), size(0) {}

    vector(std::initializer_list<T> init_list) : vector() {
        for (const T& item : init_list) {
            push_back(item);
        }
    }

    /// @brief 在列表尾部添加数据
    /// @param value 要添加的数据
    void push_back(const T& value){
        if(size == capacity){
            resize(capacity == 0 ? 1 : static_cast<size_t>(std::ceil(capacity * 1.5)));
        }
        data[size++] = value;
    }

    /// @brief 在列表尾部原地构造数据
    /// @tparam Args 构造函数参数类型
    /// @param args 构造函数参数
    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (size == capacity) {
            resize(capacity == 0 ? 1 : static_cast<size_t>(std::ceil(capacity * 1.5)));
        }
        new (&data[size++]) T(std::forward<Args>(args)...);
    }

    T& operator[](size_t index) {
        if (index >= size) {
            throw std::out_of_range("Index out of range");
        }
        return data[index];
    }

    const T& operator[](size_t index) const {
        if (index >= size) {
            throw std::out_of_range("Index out of range");
        }
        return data[index];
    }

    /// @brief 获取列表中当前的元素数量
    /// @return 
    size_t get_size() const {
        return size;
    }

    /// @brief 获取列表占用内存大小
    /// @return 
    size_t get_capacity() const {
        return capacity;
    }

    /// @brief 返回指向第一个元素的迭代器
    /// @return 指向第一个元素的迭代器
    Iterator<T> begin() {
        return Iterator<T>(data.get());
    }

    /// @brief 返回指向第一个元素的常量迭代器
    /// @return 指向第一个元素的常量迭代器
    Iterator<const T> begin() const {
        return Iterator<const T>(data.get());
    }

    /// @brief 返回指向末尾后一个元素的迭代器
    /// @return 指向末尾后一个元素的迭代器
    Iterator<T> end() {
        return Iterator<T>(data.get() + size);
    }

    /// @brief 返回指向末尾后一个元素的常量迭代器
    /// @return 指向末尾后一个元素的常量迭代器
    Iterator<const T> end() const {
        return Iterator<const T>(data.get() + size);
    }
};

}
