#include <chrono>
#include <memory>

#include "err.h"
#include "epoll.h"

class Timer : public IOPollable {
public:
    Timer();
    ~Timer();
    void init_periodic(std::chrono::nanoseconds timeout);
    void init_oneshoot(std::chrono::nanoseconds timeout);
    void init(int clockid, const itimerspec * value, int flags);
    void on_shoot_func(OnEventFunc func);
    int epollIN() override;
    error_c start_with(IOLoop* loop) override;
    void cleanup() override;
private:
    class TimerImpl;
    std::unique_ptr<TimerImpl> _impl;
};
