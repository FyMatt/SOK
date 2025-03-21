#pragma once
#include "vector.hpp"
#include <thread>
#include <queue>
#include <future>
#include <condition_variable>
#include <atomic>
#include "function.hpp"

namespace mstd {

class ThreadPool {
public:
    /// @brief 创建线程池
    /// @param numThreads 线程池的线程数量 
    ThreadPool(size_t numThreads): _stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            _workers.emplace_back([this] {
                for (;;) {
                    mstd::Function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->_queueMutex);
                        this->_condition.wait(lock, [this] { return this->_stop.load() || !this->_tasks.empty(); });
                        if (this->_stop.load() && this->_tasks.empty()) return;
                        task = std::move(this->_tasks.front());
                        this->_tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool(){
        _stop.store(true);
        _condition.notify_all();
        for (std::thread& worker : _workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    /// @brief 添加任务到线程池
    /// @tparam F 
    /// @tparam ...Args 
    /// @param f 
    /// @param ...args 
    /// @return 
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>{
        using returnType = typename std::result_of<F(Args...)>::type;
    
        auto task = std::make_shared<std::packaged_task<returnType()>>(mstd::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<returnType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            if (_stop.load()) throw std::runtime_error("enqueue on stopped ThreadPool");
            _tasks.emplace([task]() { (*task)(); });
        }
        _condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> _workers;
    std::queue<mstd::Function<void()>> _tasks;

    std::mutex _queueMutex;
    std::condition_variable _condition;
    std::atomic<bool> _stop;
};
}
