#pragma once
#include <atomic>
#include <memory>
#include <utility>
#include <thread>

namespace mstd {

template <typename T>
class LockFreeQueue {
private:
    struct Node {
        std::shared_ptr<T> data;
        Node* next;
        template <typename U>
        Node(U&& value) : data(std::make_shared<T>(std::forward<U>(value))), next(nullptr) {}
    };

    std::atomic<Node*> head;
    std::atomic<Node*> tail;

public:
    LockFreeQueue() : head(new Node(T())), tail(head.load(std::memory_order_relaxed)) {}

    /// @brief 析构函数, 释放内存
    ~LockFreeQueue() {
        while (Node* old_head = head.load(std::memory_order_relaxed)) {
            head.store(old_head->next, std::memory_order_relaxed);
            delete old_head;
        }
    }

    /// @brief 入无锁队列
    /// @tparam U 数据类型
    /// @param value 入队数据
    template <typename U>
    void enqueue(U&& value) {
        Node* new_node = new Node(std::forward<U>(value));
        Node* old_tail = tail.load(std::memory_order_relaxed);
        while (!tail.compare_exchange_weak(old_tail, new_node, std::memory_order_release, std::memory_order_relaxed)) {
            old_tail = tail.load(std::memory_order_relaxed);
        }
        old_tail->next = new_node;
    }

    bool empty() const {
        return head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire);
    }

    /// @brief 出无锁队列
    /// @return std::shared_ptr<T> 出队数据
    std::shared_ptr<T> dequeue() {
        Node* old_head = head.load(std::memory_order_acquire);
        while (old_head != tail.load(std::memory_order_acquire) && !head.compare_exchange_weak(old_head, old_head->next, std::memory_order_release, std::memory_order_relaxed)) {
            std::this_thread::yield();
            old_head = head.load(std::memory_order_acquire);
        }
        // 这里判断下是否为空
        if (old_head == tail.load(std::memory_order_acquire)) {
            return std::shared_ptr<T>();
        }
        std::shared_ptr<T> res = old_head->next->data;
        Node* new_head = old_head->next;
        head.store(new_head, std::memory_order_relaxed);
        delete old_head;
        return res;
    }
};
}