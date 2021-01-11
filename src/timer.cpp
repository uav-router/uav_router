#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "log.h"
#include "timer.h"

class Timer::TimerImpl {
public:
    TimerImpl() = default;
    void init_periodic(std::chrono::nanoseconds timeout) {
        _ts.it_value.tv_sec  = _ts.it_interval.tv_sec  = timeout.count() / 1000000000;
        _ts.it_value.tv_nsec = _ts.it_interval.tv_nsec = timeout.count() % 1000000000;
        _clockid = CLOCK_MONOTONIC;
        _flags = 0;
    }
    void init_oneshoot(std::chrono::nanoseconds timeout) {
        _ts.it_value.tv_sec  = timeout.count() / 1000000000;
        _ts.it_value.tv_nsec = timeout.count() % 1000000000;
        _ts.it_interval.tv_sec  = 0;
        _ts.it_interval.tv_nsec = 0;
        _clockid = CLOCK_MONOTONIC;
        _flags = 0;
    }
    void init(int clockid, const itimerspec * value, int flags) {
        _clockid = clockid;
        _flags = flags;
        _ts = *value;
    }

    int _fd = -1;
    itimerspec _ts;
    int _clockid;
    int _flags;
    uint64_t _result=0;
    OnEventFunc _on_shoot;
    IOLoop* _loop;
};


Timer::Timer():IOPollable("timer"),_impl{new TimerImpl{}} {}
Timer::~Timer() = default;

void Timer::init_periodic(std::chrono::nanoseconds timeout) {
    _impl->init_periodic(timeout);
}
void Timer::init_oneshoot(std::chrono::nanoseconds timeout) {
    _impl->init_oneshoot(timeout);
}
void Timer::init(int clockid, const itimerspec * value, int flags) {
    _impl->init(clockid, value, flags);
}
void Timer::on_shoot_func(OnEventFunc func) {
    _impl->_on_shoot = func;
}

auto Timer::epollIN() -> int {
    int ret = read(_impl->_fd, &_impl->_result, sizeof(_impl->_result));
    if (ret!=sizeof(_impl->_result)) {
        errno_c err;
        on_error(err,"Wrong timerfd read");
    } else {
        if (_impl->_on_shoot) _impl->_on_shoot();
        if (!(_impl->_ts.it_interval.tv_sec || _impl->_ts.it_interval.tv_nsec )) {
            _impl->_loop->del(_impl->_fd, this);
            cleanup();
        }
    }
    return HANDLED;
}
auto Timer::start_with(IOLoop* loop) -> error_c {
    _impl->_fd = timerfd_create(_impl->_clockid, TFD_NONBLOCK);
    if (_impl->_fd==-1) {
        return errno_c("timerfd_create");
    }
    errno_c ret = err_chk(timerfd_settime(_impl->_fd, _impl->_flags, &_impl->_ts, nullptr));
    if (ret) {
        close(_impl->_fd);
        ret.add_place("timerfd_settime");
        return ret;
    }
    _impl->_loop = loop;
    return loop->add(_impl->_fd, EPOLLIN, this);
}

void Timer::stop() {
    _impl->_loop->del(_impl->_fd, this);
    cleanup();
}

void Timer::cleanup() {
    close(_impl->_fd);
}