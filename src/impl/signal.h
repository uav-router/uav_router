#ifndef __SIGNAL_IMPL__
#define __SIGNAL_IMPL__

#include <unistd.h>
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <csignal>

#include "../log.h"
#include "../loop.h"


class SignalImpl : public IOPollable, public Signal {
public:
    using OnSignalFunc  = std::function<bool(signalfd_siginfo*)>;
    SignalImpl(Poll* poll):IOPollable("signal"),_poll(poll) {
        sigemptyset(&mask);
    }
    ~SignalImpl() override {
        if (_fd!=-1) {
            _poll->del(_fd, this);
            cleanup();
        }
    }
    auto epollIN() -> int override {
        while(true) {
            signalfd_siginfo fdsi;
            ssize_t s = read(_fd, &fdsi, sizeof(fdsi));
            if (s==-1) {
                errno_c err("signal read");
                if (err == std::error_condition(std::errc::resource_unavailable_try_again)) break;
                on_error(err);
                return HANDLED;
            } 
            if (s != sizeof(fdsi)) {
                log.error()<<"_sig_fd Wrong read size "<<s<<std::endl;
                return HANDLED;
            }
            if (_on_signal) {
                if (_on_signal(&fdsi)) break;
            }
        }
        return HANDLED;
    }
    void cleanup() override {
        log.debug()<<"cleanup signal"<<std::endl;
        if (_fd != -1) {
            //_poll->del(_fd, this);
            close(_fd);
            _fd = -1;
        }
    }
    
    auto init(std::initializer_list<int> signals, OnSignalFunc handler) -> error_c override {
        log.debug()<<"init signal"<<std::endl;
        sigemptyset(&mask);
        for(auto signal: signals) {
            sigaddset(&mask, signal);
        }
        _on_signal = handler;
        error_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, nullptr),"sigprocmask");
        if (ret) return ret;
        _fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (_fd==-1) {
            errno_c err("signal_fd");
            return err;
        }
        ret = _poll->add(_fd, EPOLLIN, this);
        if (ret) { ret.add_place("loop add");
        }
        return ret;
    }
private:
    sigset_t mask;
    OnSignalFunc _on_signal;
    int _fd = -1;
    Poll* _poll;
    inline static Log::Log log {"signal"};
};

#endif //__SIGNAL_IMPL__