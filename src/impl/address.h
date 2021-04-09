#ifndef __ADDRESS_IMPL_H__
#define __ADDRESS_IMPL_H__
#include <chrono>
using namespace std::chrono_literals;
#include <functional>
#include <csignal>
#include <sys/epoll.h>
#include "../sockaddr.h"
#include "../loop.h"
#include "../log.h"

class AddrInfo final : public IOPollable, public error_handler {
public:
    using callback_t = std::function<void(addrinfo*, std::error_code& ec)>;
    AddrInfo(IOLoopSvc *loop) :IOPollable("addrinfo"),_poll(loop->poll()) {
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = 0;
        _hints.ai_protocol = 0;
        _hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
    }
    ~AddrInfo() final {
        if (_sfd!=-1) {
            _poll->del(_sfd, this);
        }
        cleanup();
    }
    auto init(const std::string& name, const std::string& port="", 
             int family = AF_UNSPEC, int socktype = 0, int protocol = 0, 
             int flags = AI_V4MAPPED | AI_ADDRCONFIG) -> error_c {
        if (_sfd!=-1) {
            _poll->del(_sfd, this);
        }
        cleanup();
        _name = name; 
        _port = port;
        _hints.ai_family = family;
        _hints.ai_socktype = socktype;
        _hints.ai_protocol = protocol;
        _hints.ai_flags = flags;
        req.ar_name = _name.c_str();
        if (_port.empty()) {
            req.ar_service = nullptr;
        } else {
            req.ar_service = _port.c_str();
        }
        req.ar_request = &_hints;
        se.sigev_notify = SIGEV_SIGNAL;
        se.sigev_signo = SIGUSR1;
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGUSR1);
        errno_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, nullptr));
        if (ret) {
            ret.add_place("sigprocmask");
            return ret;
        }
        _sfd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (_sfd == -1) {
            return errno_c("signalfd");
        }
        ret = _poll->add(_sfd, EPOLLIN, this);
        if (ret) {
            ret.add_place("IOLoop add");
            close(_sfd);
            return ret;
        }
        se.sigev_value.sival_ptr = this;
        gaicb * ptr = &req;
        eai_code ec = getaddrinfo_a(GAI_NOWAIT, &ptr, 1, &se);
        if (on_error(ec, "getaddrinfo")) {
            ret = _poll->del(_sfd, this);
            on_error(ret,"IOLoop del");
            close(_sfd);
            return ec;
        }
        log.debug()<<"start_with ends"<<std::endl;
        return error_c();
    }
    auto on_result(callback_t func) -> AddrInfo& {
        _on_result = func;
        return *this;
    }
    void on_result(addrinfo* ai, std::error_code& ec) {
        if (_on_result) _on_result(ai,ec);
    }

    auto epollIN() -> int override {
        while(true) {
            signalfd_siginfo fdsi;
            ssize_t s = read(_sfd, &fdsi, sizeof(fdsi));
            if (s==-1) {
                errno_c err;
                if (err == std::error_condition(std::errc::resource_unavailable_try_again)) break;
                on_error(err,"events read");
                break;
            }
            if (s != sizeof(fdsi)) {
                log.error()<<"AddrInfo->Wrong read size "<<s<<std::endl;
                continue;
            }
            if (fdsi.ssi_signo != SIGUSR1) {
                log.error()<<"AddrInfo->Wrong signal no "<<fdsi.ssi_signo<<std::endl;
                continue;
            }
            if (fdsi.ssi_code != SI_ASYNCNL) {
                log.error()<<"AddrInfo->Wrong signal code "<<fdsi.ssi_code<<std::endl;
                continue;
            }
            error_c ec = eai_code(&req);
            on_result(req.ar_result, ec);
            if (req.ar_result) { freeaddrinfo(req.ar_result);
            }
            _poll->del(_sfd, this);
            cleanup();
            break;
        }
        return HANDLED;
    }
    void cleanup() override {
        if (_sfd!=-1) {
            close(_sfd);
            _sfd = -1;
        }
        log.debug()<<"Cleanup called"<<std::endl;
    }
private:
    std::string _name;
    std::string _port;
    addrinfo _hints;
    gaicb req;
    sigevent se;
    int _sfd = -1;
    eai_code res;
    callback_t _on_result;
    Poll *_poll;
    inline static Log::Log log {"addrinfo"};
};

class AddressResolverImpl : public AddressResolver {
public:
    AddressResolverImpl(IOLoopSvc *loop):_ai(loop),_timer(loop->timer()) {
        _timer->shoot([this]() { requery(); });
        _ai.on_result([this](addrinfo* ai, std::error_code& ec){
            on_addrinfo(ai,ec);
        });
    }

    auto init(const std::string& host, uint16_t port, OnResolveFunc handler) -> error_c override {
        _on_resolve = handler;
        _host = host;
        _port = port;
        auto on_err = [this](error_c& ec){ on_error(ec,"address resolver");};
        _ai.on_error(on_err);
        _timer->on_error(on_err);
        return requery();
    }

    auto requery() -> error_c override {
        error_c ret = _ai.init(_host,std::to_string(_port),_family,_socktype,_protocol, _flags);
        if (on_error(ret,"address_resolving")) return ret;
        return error_c();
    }
    
    void on_addrinfo(addrinfo* ai, std::error_code& ec) {
        error_c err(ec.value(),ec.category());
        if (on_error(err,"addrinfo")) {
            error_c ret = _timer->arm_oneshoot(5s);
            on_error(ret,"arm timer");
            return;
        }
        if (_on_resolve) _on_resolve(ai);
    }

    auto family(int f) -> AddressResolver&   override {
        _family = f;
        return *this;
    }
    auto socktype(int t) -> AddressResolver& override {
        _socktype = t;
        return *this;
    }
    auto protocol(int p) -> AddressResolver& override {
        _protocol = p;
        return *this;
    }
    auto flags(int f) -> AddressResolver&    override {
        _flags = f;
        return *this;
    }
    auto add_flags(int f) -> AddressResolver& override {
        _flags |= f;
        return *this;
    }
    
    int _family = AF_UNSPEC;
    int _socktype = 0;
    int _protocol = 0;
    int _flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_CANONNAME;

    IOLoop *_loop;
    AddrInfo _ai;
    std::unique_ptr<Timer> _timer;
    OnResolveFunc _on_resolve;
    std::string _host; 
    uint16_t _port;
};

#endif  //!__ADDRESS_IMPL_H__