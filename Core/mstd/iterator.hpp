#pragma once

namespace mstd {
    
template <typename T>
class Iterator {
public:
    using pointer = T*;
    using reference = T&;

    Iterator(pointer ptr) : m_ptr(ptr) {}

    // 重载指针，返回引用类型
    reference operator*() const { return *m_ptr; }
    
    // 重载引用，返回指针类型
    pointer operator->() { return m_ptr; }

    // 前缀递增
    Iterator& operator++() { 
        m_ptr++; 
        return *this; 
    }

    // 后缀递增
    Iterator operator++(int) { 
        Iterator tmp = *this; 
        ++(*this); 
        return tmp; 
    }

    // 设置为友元函数，方便访问私有成员
    friend bool operator==(const Iterator& a, const Iterator& b) { return a.m_ptr == b.m_ptr; }
    friend bool operator!=(const Iterator& a, const Iterator& b) { return a.m_ptr != b.m_ptr; }

private:
    pointer m_ptr;
};
}
