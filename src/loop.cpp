#include <memory>
#include <iostream>
#include <forward_list>

#include <csignal>
#include <unistd.h>
#include <cstring>
#include <utility>

#include "err.h"
#include "inc/poll.h"
#include "inc/udev.h"
#include "log.h"
#include "loop.h"

#include "impl/epoll.h"
#include "impl/signal.h"
#include "impl/timer.h"
#include "impl/udev.h"
#include "impl/uart.h"


//----------------------------------------


class IOLoopImpl : public IOLoopSvc, public Poll {
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
    auto uart(const std::string& name) -> std::unique_ptr<UART> override {
        return std::unique_ptr<UART>(new UARTImpl{name,this});
    }
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
    auto timer() -> std::unique_ptr<Timer> override {
        return std::unique_ptr<Timer>(new TimerImpl{this});
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

    void block_udev() override {
        _block_udev = true;
    }
    auto udev() -> UdevLoop* override {
        if (_block_udev) return nullptr;
        if (_udev) return _udev.get();
        _udev = std::make_unique<UDevIO>(this);
        if (_udev->get_ec()) {
            _udev.reset();
            return nullptr;
        }
        _udev->on_error([this](error_c ec){ on_error(ec);});
        return _udev.get();
    }
    

    Epoll _epoll;
    int _epoll_events_number;
    bool _loop_stop = false;
    bool _block_udev = false;
    std::forward_list<IOPollable*> _iowatches;
    std::unique_ptr<Signal> ctrlC_handler;
    std::unique_ptr<UDevIO> _udev;
    inline static Log::Log log {"ioloop"};
};

auto IOLoop::loop(int pool_events) -> std::unique_ptr<IOLoop> {
    return std::make_unique<IOLoopImpl>(pool_events);
}

