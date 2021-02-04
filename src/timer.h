#ifndef __TIMER_H__
#define __TIMER_H__
#include <chrono>
#include <memory>

#include "err.h"
#include "loop.h"

class Timer : public IOPollable, public error_handler {
public:
    Timer();
    ~Timer() override;
    auto arm_periodic(std::chrono::nanoseconds timeout) -> error_c;
    auto arm_oneshoot(std::chrono::nanoseconds timeout, bool autoclean = true) -> error_c;
    auto arm(const itimerspec * value, int flags, bool autoclean = true) -> error_c;
    void init(int clockid);
    void on_shoot_func(OnEventFunc func);
    auto epollIN() -> int override;
    auto start_with(IOLoop* loop) -> error_c override;
    void set_loop(IOLoop* loop);
    void stop();
    void cleanup() override;
private:
    class TimerImpl;
    std::unique_ptr<TimerImpl> _impl;
};

#endif // __TIMER_H__