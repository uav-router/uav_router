#ifndef __TIMER_IMPL__H__
#define __TIMER_IMPL__H__

#include <sys/timerfd.h>
#include "../log.h"
#include "../loop.h"
#include "epoll.h"

class TimerImpl : public IOPollable, public Timer {
public:
    TimerImpl(IOLoopSvc* loop):IOPollable("timer"),_poll(loop->poll()) {}
    ~TimerImpl() override { stop(); }
    auto arm_periodic(std::chrono::nanoseconds timeout) -> error_c override {
        itimerspec ts;
        ts.it_value.tv_sec  = ts.it_interval.tv_sec  = timeout.count() / 1000000000;
        ts.it_value.tv_nsec = ts.it_interval.tv_nsec = timeout.count() % 1000000000;
        return arm(&ts,0);
    };
    auto arm_oneshoot(std::chrono::nanoseconds timeout) -> error_c override {
        itimerspec ts;
        ts.it_value.tv_sec  = timeout.count() / 1000000000;
        ts.it_value.tv_nsec = timeout.count() % 1000000000;
        ts.it_interval.tv_sec  = 0;
        ts.it_interval.tv_nsec = 0;
        return arm(&ts,0);
    };
    auto arm(const itimerspec * value, int flags) -> error_c override {
        if (_fd==-1) {
            _fd = timerfd_create(_clockid, TFD_NONBLOCK);
            if (_fd==-1) { return errno_c("timerfd_create");
            }
            errno_c ret = _poll->add(_fd, EPOLLIN, this);
            if (ret) {
                close(_fd);
                ret.add_place("loop add");
                _fd = -1;
                return ret;
            }
        }
        auto ret = err_chk(timerfd_settime(_fd, flags, value, nullptr));
        if (ret) {
            ret.add_place("timerfd_settime");
        }
        return ret;
    };
    auto clockid(int id) -> Timer& override {
        _clockid = id;
        return *this;
    };

    auto shoot(OnEventFunc func) -> Timer& override {
        _on_shoot = func;
        return *this;
    };

    auto epollIN() -> int override {
        uint64_t _result;
        int ret = read(_fd, &_result, sizeof(_result));
        if (ret==sizeof(_result)) {
            if (_on_shoot) _on_shoot();
        } else {
            errno_c err;
            on_error(err,"Wrong timerfd read");
        }
        return HANDLED;
    }

    void stop() override {
        if (_fd!=-1) {
            _poll->del(_fd, this);
            cleanup();
        }
    }

    void cleanup() override {
        if (_fd!=-1) {
            close(_fd);
            _fd = -1;
        }
    }
    int _clockid = CLOCK_MONOTONIC;
    int _fd = -1;
    OnEventFunc _on_shoot;
    Poll* _poll;
    //inline static Log::Log log {"timer"};
};
#endif  //!__TIMER_IMPL__H__