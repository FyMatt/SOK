#pragma once
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <tuple>

namespace mstd {

template<typename Signature>
class Function;

template<typename R, typename... Args>
class Function<R(Args...)> {
public:
    // 构造函数
    Function() = default;

    // 拷贝构造函数
    template<typename F>
    Function(F&& f) : callable_(std::make_unique<CallableImpl<F>>(std::forward<F>(f))) {}

    // 重载()运算符, 用于调用函数对象
    R operator()(Args... args) const {
        if (callable_) {
            return callable_->invoke(std::forward<Args>(args)...);
        } else {
            throw std::runtime_error("Function object is empty");
        }
    }

    // 重载bool运算符, 用于判断函数对象是否为空
    explicit operator bool() const noexcept {
        return static_cast<bool>(callable_);
    }

private:
    // CallableBase 类模板用于存储函数对象的基类
    struct CallableBase {
        virtual ~CallableBase() = default;
        virtual R invoke(Args... args) const = 0;
    };

    // CallableImpl 类模板用于存储函数对象
    template<typename F>
    struct CallableImpl : CallableBase {
        CallableImpl(F&& f) : f_(std::forward<F>(f)) {}
        R invoke(Args... args) const override {
            return f_(std::forward<Args>(args)...);
        }
        F f_;
    };

    std::unique_ptr<CallableBase> callable_;
};

// 辅助函数，用于调用绑定的函数
template<typename F, typename Tuple, typename... Args, std::size_t... I>
auto invoke_impl(F&& f, Tuple& t, Args&&... args, std::index_sequence<I...>) {
    return f(std::get<I>(t)..., std::forward<Args>(args)...);
}


// 实现类似std::bind的功能
template<typename F, typename... BoundArgs>
auto bind(F&& f, BoundArgs&&... boundArgs) {
    return [f = std::forward<F>(f), boundArgs = std::make_tuple(std::forward<BoundArgs>(boundArgs)...)](auto&&... remainingArgs) mutable {
        return invoke_impl(f, boundArgs, std::forward<decltype(remainingArgs)>(remainingArgs)..., 
                           std::make_index_sequence<std::tuple_size<decltype(boundArgs)>::value>{});
    };
}

}