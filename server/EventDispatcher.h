#ifndef MYSERVER_EVENTDISPATCHER_H
#define MYSERVER_EVENTDISPATCHER_H

#include <functional>
#include <vector>
#include <shared_mutex>
#include <utility>

class IInvokable {
public:
    virtual ~IInvokable() = default;
    virtual void invoke() = 0;
};


template<typename... Args>
class EventHandler : public IInvokable {
public:
    using FunctionType = std::function<void(Args...)>;

    explicit EventHandler(FunctionType func)
            : function_(std::move(func)) {}

    void invoke() override {
        if (function_ && args_) {
            std::apply(function_, *args_);
        }
    }

    void set_args(Args... args) {
        args_ = std::make_shared<std::tuple<Args...>>(std::forward<Args>(args)...);
    }

private:
    FunctionType function_;
    std::shared_ptr<std::tuple<Args...>> args_;
};


class Event {
public:
    Event() = default;

    template<typename... Args>
    void operator+=(std::function<void(Args...)> handler) {
        std::unique_lock lock(mutex_);

        handlers_.emplace_back(
            std::make_unique<EventHandler<Args...>>(handler)
        );
    }

    template<typename... Args>
    void operator()(Args&&... args) {
        std::shared_lock lock(mutex_);
        for (auto& handler : handlers_) {
            if (auto h = dynamic_cast<EventHandler<std::decay_t<Args>...>*>(handler.get())) {
                h->set_args(std::forward<Args>(args)...);
                h->invoke();
            }
        }
    }

private:
    std::vector<std::unique_ptr<IInvokable>> handlers_;
    mutable std::shared_mutex mutex_;
};


#endif //MYSERVER_EVENTDISPATCHER_H
