#include <memory>
#include <iostream>
#include <forward_list>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <csignal>
#include <cstring>

#include "log.h"
#include "endpoint.h"

static Log::Log log("ioloop");

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
};


class Epoll {
public:
    Epoll() = default;

    auto create(int flags = 0) -> errno_c {
        efd = epoll_create1(flags);
        if (efd==-1) return errno_c("epoll_create");
        return errno_c(0);
    }
    ~Epoll() {
        if (efd != -1) close(efd);
    }
    auto add(int fd, uint32_t events, void* ptr = nullptr) -> errno_c {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev), "epoll_ctl add");
    }
    auto mod(int fd, uint32_t events, void* ptr = nullptr) -> errno_c {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev), "epoll_ctl mod");
    }
    auto del(int fd) -> errno_c {
        epoll_event ev;
        return err_chk(epoll_ctl(efd, EPOLL_CTL_DEL, fd, &ev), "epoll_ctl del");
    }
    auto wait(epoll_event *events, int maxevents, int timeout = -1, const sigset_t *sigmask = nullptr) -> int {
        if (sigmask) return epoll_pwait(efd, events, maxevents, timeout, sigmask);
        return epoll_wait(efd, events, maxevents, timeout);
    }
private:
    int efd = -1;
};


class IOLoopImpl : public IOLoop, public Poll {
public:
    IOLoopImpl(int size): _epoll_events_number(size) {
        on_error([](error_c& ec){ log.error()<<"ioloop"<<ec<<std::endl;} );
        errno_c ret = _epoll.create();
        if (ret) {
            ret.add_place("IOLoop");
            log.error()<<ret<<std::endl;
            throw std::system_error(ret, ret.place());
        }
    }
    // loop items
    //auto uart(const std::string& name) -> std::unique_ptr<UART> override {}
    //auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> override {}
    //auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> override {}
    //auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> override {}
    //auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> override {}
    //auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> override {}
    // stats
    //auto stats() -> StatHandler& override {}
    // run
    void run() override { 
        log.debug()<<"run start"<<std::endl;
        //register_report(&_impl->stat,100ms);
        std::vector<epoll_event> events(_epoll_events_number);
        _loop_stop = false;
        while(!_loop_stop) {
            int r = _epoll.wait(events.data(), events.size());
            if (r < 0) {
                errno_c err;
                if (err == std::error_condition(std::errc::interrupted)) { continue;
                } else { on_error(err);
                }
            }
            for (int i = 0; i < r; i++) {
                auto* obj = static_cast<IOPollable *>(events[i].data.ptr);
                auto evs = events[i].events;
                //log.debug()<<obj->name<<" event "<<evs<<" obj "<<obj<<std::endl;
                if (obj->epollEvent(evs)) continue;
                if (evs & EPOLLIN) {
                    //log.debug()<<"EPOLLIN"<<std::endl;
                    int ret = obj->epollIN();
                    //auto s = stat.time_measure["in"].measure();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLIN not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLOUT) {
                    //log.debug()<<"EPOLLOUT"<<std::endl;
                    //auto s = _impl->stat.time_measure["out"].measure();
                    int ret = obj->epollOUT();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLOUT not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLPRI) {
                    //log.debug()<<"EPOLLPRI"<<std::endl;
                    //auto s = _impl->stat.time_measure["pri"].measure();
                    int ret = obj->epollPRI();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLPRI not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLERR) {
                    //log.debug()<<"EPOLLERR"<<std::endl;
                    //auto s = _impl->stat.time_measure["err"].measure();
                    int ret = obj->epollERR();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLERR not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLHUP) {
                    //log.debug()<<"EPOLLHUP"<<std::endl;
                    //auto s = _impl->stat.time_measure["hup"].measure();
                    int ret = obj->epollHUP();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLHUP not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLRDHUP) {
                    //log.debug()<<"EPOLLRDHUP"<<std::endl;
                    //auto s = _impl->stat.time_measure["rdhup"].measure();
                    int ret = obj->epollRDHUP();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLRDHUP not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
            }
        }
        for (auto w : _iowatches) { w->cleanup();
        }
        _iowatches.clear();
        log.debug()<<"run end"<<std::endl;
    }
    void stop() override {_loop_stop = true;}
    
    //auto handle_udev() -> error_c override {}
    //auto handle_zeroconf() -> error_c override {}
    auto poll() -> Poll* override { return this; }
    auto add(int fd, uint32_t events, IOPollable* obj) -> errno_c override {
        errno_c ret = _epoll.add(fd, events, obj);
        if (!ret) { _iowatches.push_front(obj);
        }
        return ret;
    }
    auto mod(int fd, uint32_t events, IOPollable* obj) -> errno_c override {
        return _epoll.mod(fd, events, obj);
    }
    auto del(int fd, IOPollable* obj) -> errno_c override {
        errno_c ret = _epoll.del(fd);
        if (!ret) { _iowatches.remove(obj);
        }
        return ret;
    }

    auto signal_handler() -> std::unique_ptr<Signal> override {
        return std::unique_ptr<Signal>(new SignalImpl{this});
    }

    auto handle_CtrlC() -> error_c override {
        ctrlC_handler = signal_handler();
        return ctrlC_handler->init({SIGINT,SIGTERM}, [this](signalfd_siginfo* si) {
            log.info()<<"Signal: "<<strsignal(si->ssi_signo)<<std::endl;
            stop();
            ctrlC_handler.reset();
            return true;
        });
    }

    Epoll _epoll;
    int _epoll_events_number;
    bool _loop_stop = false;
    std::forward_list<IOPollable*> _iowatches;
    std::unique_ptr<Signal> ctrlC_handler;
};

auto IOLoop::loop(int pool_events) -> std::unique_ptr<IOLoop> {
    return std::make_unique<IOLoopImpl>(pool_events);
}

