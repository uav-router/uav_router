#include <memory>
#include <iostream>
#include <forward_list>

#include <csignal>
#include <netdb.h>
#include <sys/socket.h>
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
#include "impl/zeroconf.h"
#include "impl/address.h"
#include "sockaddr.h"
#include "impl/tcpcli.h"
#include "impl/tcpsvr.h"
#include "impl/udpcli.h"
#include "impl/udpsvr.h"
#include "impl/stat.h"
#include "impl/statobj.h"
#include "impl/ofile.h"


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
        return std::make_unique<UARTImpl>(name,this);
    }
    //auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> override {}
    auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> override {
        return std::make_unique<TcpClientImpl>(name,this);
    }
    auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> override {
        return std::make_unique<UdpClientImpl>(name,this);
    }
    auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> override {
        return std::make_unique<TcpServerImpl>(name,this);
    }
    auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> override {
        return std::make_unique<UdpServerImpl>(name,this);
    }
    // stats
    //auto stats() -> StatHandler& override {}
    // run
    void run() override { 
        log.debug()<<"run start"<<std::endl;
        _stat = std::make_shared<StatDurations>("loop");
        if (!_stats) {
            _stats = std::make_unique<StatHandlerImpl>(this);
            _stats->on_error([this](const error_c& ec) {on_error(ec);});
        }
        _stats->register_report(_stat, 100ms);
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
                    auto s = _stat->time["in"].measure();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLIN not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLOUT) {
                    //log.debug()<<"EPOLLOUT"<<std::endl;
                    auto s = _stat->time["out"].measure();
                    int ret = obj->epollOUT();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLOUT not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLPRI) {
                    //log.debug()<<"EPOLLPRI"<<std::endl;
                    auto s = _stat->time["pri"].measure();
                    int ret = obj->epollPRI();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLPRI not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLERR) {
                    //log.debug()<<"EPOLLERR"<<std::endl;
                    auto s = _stat->time["err"].measure();
                    int ret = obj->epollERR();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLERR not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLHUP) {
                    //log.debug()<<"EPOLLHUP"<<std::endl;
                    auto s = _stat->time["hup"].measure();
                    int ret = obj->epollHUP();
                    if (ret==IOPollable::NOT_HANDLED) log.warning()<<obj->name<<" EPOLLHUP not handled"<<std::endl;
                    if (ret==IOPollable::STOP) continue;
                }
                if (evs & EPOLLRDHUP) {
                    //log.debug()<<"EPOLLRDHUP"<<std::endl;
                    auto s = _stat->time["rdhup"].measure();
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

    auto address() -> std::unique_ptr<AddressResolver> override {
        return std::make_unique<AddressResolverImpl>(this);
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

    void block_zeroconf() override {
        _block_zeroconf = true;
        if (_zeroconf) _zeroconf.reset();
    }
    
    void zeroconf_ready(OnEvent func) override {
        _block_zeroconf = false;
        if (!_zeroconf) {
            _zeroconf = std::make_unique<AvahiImpl>(this);
        }
        _zeroconf->on_ready(func);
    }

    auto zeroconf() -> Avahi* override {
        if (_block_zeroconf) return nullptr;
        if (_zeroconf) return _zeroconf.get();
        _zeroconf = std::make_unique<AvahiImpl>(this);
        return _zeroconf.get();
    }

    auto stats() -> StatHandler* override {
        if (_stats) return _stats.get();
        _stats = std::make_unique<StatHandlerImpl>(this);
        _stats->on_error([this](const error_c& ec) {on_error(ec);});
        return _stats.get();
    }

    auto outfile() -> std::unique_ptr<OFileStream> override {
        return std::make_unique<OFileStreamImpl>();
    }

    Epoll _epoll;
    int _epoll_events_number;
    bool _loop_stop = false;
    bool _block_udev = false;
    bool _block_zeroconf = false;
    std::forward_list<IOPollable*> _iowatches;
    std::unique_ptr<Signal> ctrlC_handler;
    std::unique_ptr<UDevIO> _udev;
    std::unique_ptr<AvahiImpl> _zeroconf;
    std::unique_ptr<StatHandlerImpl> _stats;
    std::shared_ptr<StatDurations> _stat;
    inline static Log::Log log {"ioloop"};
};

auto IOLoop::loop(int pool_events) -> std::unique_ptr<IOLoop> {
    return std::make_unique<IOLoopImpl>(pool_events);
}

auto IOLoopSvc::loop(int pool_events) -> std::unique_ptr<IOLoopSvc> {
    return std::make_unique<IOLoopImpl>(pool_events);
}