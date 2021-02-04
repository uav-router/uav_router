#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include "log.h"
#include "timer.h"

class Timer::TimerImpl {
public:
    TimerImpl() = default;
    void init(int clockid) {
        _clockid = clockid;
    }

    auto arm_periodic(std::chrono::nanoseconds timeout) -> error_c {
        itimerspec ts;
        ts.it_value.tv_sec  = ts.it_interval.tv_sec  = timeout.count() / 1000000000;
        ts.it_value.tv_nsec = ts.it_interval.tv_nsec = timeout.count() % 1000000000;
        return arm(&ts,0,false);
    }
    auto  arm_oneshoot(std::chrono::nanoseconds timeout, bool autoclean) -> error_c {
        itimerspec ts;
        ts.it_value.tv_sec  = timeout.count() / 1000000000;
        ts.it_value.tv_nsec = timeout.count() % 1000000000;
        ts.it_interval.tv_sec  = 0;
        ts.it_interval.tv_nsec = 0;        
        return arm(&ts,0,autoclean);
    }
    auto arm(const itimerspec * value, int flags, bool autoclean) -> error_c {
        auto ret = err_chk(timerfd_settime(_fd, flags, value, nullptr));
        if (ret) {
            ret.add_place("timerfd_settime");
        }
        if (!ret) {
            _rearm = true;
            _periodic = value->it_interval.tv_sec || value->it_interval.tv_nsec;
            if (!_periodic) _autoclean = autoclean;
        }
        return ret;
    }

    int _fd = -1;
    int _clockid = CLOCK_MONOTONIC;
    bool _rearm = false;
    bool _periodic = false;
    bool _autoclean = true;
    uint64_t _result=0;
    OnEventFunc _on_shoot;
    IOLoop* _loop=nullptr;
};


Timer::Timer():IOPollable("timer"),_impl{new TimerImpl{}} {}
Timer::~Timer() = default;

auto Timer::arm_periodic(std::chrono::nanoseconds timeout) -> error_c {
    return _impl->arm_periodic(timeout);
}
auto Timer::arm_oneshoot(std::chrono::nanoseconds timeout, bool autoclean) -> error_c {
    return _impl->arm_oneshoot(timeout, autoclean);
}
void Timer::init(int clockid) {
    _impl->init(clockid);
}
auto Timer::arm(const itimerspec * value, int flags, bool autoclean) -> error_c {
    return _impl->arm(value, flags, autoclean);
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
        _impl->_rearm = false;
        if (_impl->_on_shoot) _impl->_on_shoot();
        if (_impl->_autoclean && !(_impl->_periodic || _impl->_rearm)) {
            _impl->_loop->del(_impl->_fd, this);
            cleanup();
        }
    }
    return HANDLED;
}

void Timer::set_loop(IOLoop* loop) {
    if (loop) _impl->_loop = loop;
}

auto Timer::start_with(IOLoop* loop) -> error_c {
    if (loop==nullptr && _impl->_loop==nullptr) return errno_c(EINVAL,"no loop specified");

    _impl->_fd = timerfd_create(_impl->_clockid, TFD_NONBLOCK);
    if (_impl->_fd==-1) {
        return errno_c("timerfd_create");
    }
    if (loop) _impl->_loop = loop;
    errno_c ret = _impl->_loop->add(_impl->_fd, EPOLLIN, this);
    if (ret) {
        close(_impl->_fd);
        ret.add_place("loop add");
        _impl->_fd = -1;
    }
    return ret;
    
}

void Timer::stop() {
    if (_impl->_fd!=-1) {
        _impl->_loop->del(_impl->_fd, this);
        cleanup();
    }
}

void Timer::cleanup() {
    if (_impl->_fd!=-1) {
        close(_impl->_fd);
    }
}