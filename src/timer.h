#ifndef __TIMER_H__
#define __TIMER_H__
#include <chrono>
#include <memory>

#include "err.h"
#include "epoll.h"

class Timer : public IOPollable, public error_handler {
public:
    Timer();
    ~Timer() override;
    void init_periodic(std::chrono::nanoseconds timeout);
    void init_oneshoot(std::chrono::nanoseconds timeout);
    void init(int clockid, const itimerspec * value, int flags);
    void on_shoot_func(OnEventFunc func);
    auto epollIN() -> int override;
    auto start_with(IOLoop* loop) -> error_c override;
    void stop();
    void cleanup() override;
private:
    class TimerImpl;
    std::unique_ptr<TimerImpl> _impl;
};

#endif // __TIMER_H__