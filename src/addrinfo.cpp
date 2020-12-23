#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <chrono>
using namespace std::chrono_literals;

#include "log.h"
#include "addrinfo.h"
#include "timer.h"

class AddrInfo : public IOPollable, public error_handler {
public:
    using callback_t = std::function<void(addrinfo*, std::error_code& ec)>;
    AddrInfo();
    ~AddrInfo();
    void init(const std::string& name, const std::string& port="", 
             int family = AF_UNSPEC, int socktype = 0, int protocol = 0, 
             int flags = AI_V4MAPPED | AI_ADDRCONFIG);
    void on_result_func(callback_t func);
    error_c start_with(IOLoop* loop) override;
    int epollIN() override;
    void cleanup() override;
private:
    class AddrInfoImpl;
    std::unique_ptr<AddrInfoImpl> _impl;
};


class AddrInfo::AddrInfoImpl {
public:
    AddrInfoImpl() {
        _hints.ai_family = AF_UNSPEC;
        _hints.ai_socktype = 0;
        _hints.ai_protocol = 0;
        _hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
    }
    void init(const std::string& name, const std::string& port, 
             int family, int socktype, int protocol, int flags) {
        _name = name; 
        _port = port;
        _hints.ai_family = family;
        _hints.ai_socktype = socktype;
        _hints.ai_protocol = 0;
        _hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
        req.ar_name = _name.c_str();
        if (_port.empty()) {
            req.ar_service = 0;
        } else {
            req.ar_service = _port.c_str();
        }
        req.ar_request = &_hints;
        se.sigev_notify = SIGEV_SIGNAL;
        se.sigev_signo = SIGUSR1;
    }
    void on_result(addrinfo* ai, std::error_code& ec) {
        if (_on_result) _on_result(ai,ec);
    }
    std::string _name;
    std::string _port;
    addrinfo _hints;
    gaicb req;
    sigevent se;
    int _sfd;
    eai_code res;
    callback_t _on_result;
    IOLoop* _loop;
};

AddrInfo::AddrInfo():IOPollable("addrinfo"),_impl{new AddrInfoImpl{}} {}

AddrInfo::~AddrInfo() {}

void AddrInfo::init(const std::string& name, const std::string& port, 
         int family, int socktype, int protocol, int flags) {
    _impl->init(name, port, family, socktype, protocol, flags);
}

void AddrInfo::on_result_func(callback_t func) {
    _impl->_on_result = func;
}

error_c AddrInfo::start_with(IOLoop* loop) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    errno_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, NULL));
    if (ret) {
        ret.add_place("sigprocmask");
        return ret;
    }
    _impl->_sfd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (_impl->_sfd == -1) {
        return errno_c("signalfd");
    }
    _impl->_loop = loop;
    ret = loop->add(_impl->_sfd, EPOLLIN, this);
    if (ret) {
        ret.add_place("IOLoop add");
        close(_impl->_sfd);
        return ret;
    }
    _impl->se.sigev_value.sival_ptr = this;
    gaicb * ptr = &_impl->req;
    eai_code ec = getaddrinfo_a(GAI_NOWAIT, &ptr, 1, &_impl->se);
    if (on_error(ec, "getaddrinfo")) {
        ret = loop->del(_impl->_sfd, this);
        on_error(ret,"IOLoop del");
        close(_impl->_sfd);
        return ec;
    }
    log::debug()<<"start_with ends"<<std::endl;
    return error_c();
}

int AddrInfo::epollIN() {
    while(true) {
        signalfd_siginfo fdsi;
        ssize_t s = read(_impl->_sfd, &fdsi, sizeof(fdsi));
        if (s==-1) {
            errno_c err;
            if (err == std::error_condition(std::errc::resource_unavailable_try_again)) break;
            on_error(err,"events read");
            break;
        }
        if (s != sizeof(fdsi)) {
            log::error()<<"AddrInfo->Wrong read size "<<s<<std::endl;
            continue;
        }
        if (fdsi.ssi_signo != SIGUSR1) {
            log::error()<<"AddrInfo->Wrong signal no "<<fdsi.ssi_signo<<std::endl;
            continue;
        }
        if (fdsi.ssi_code != SI_ASYNCNL) {
            log::error()<<"AddrInfo->Wrong signal code "<<fdsi.ssi_code<<std::endl;
            continue;
        }
        error_c ec = eai_code(&_impl->req);
        _impl->on_result(_impl->req.ar_result, ec);
        if (_impl->req.ar_result) { freeaddrinfo(_impl->req.ar_result);
        }
        _impl->_loop->del(_impl->_sfd, this);
        cleanup();
        break;
    }
    return HANDLED;
}

void AddrInfo::cleanup() {
    close(_impl->_sfd);
    log::debug()<<"Cleanup called"<<std::endl;
}

class AddressResolver::AddressResolverImpl : public error_handler {
public:
    void init_resolving_client(const std::string& host, int port, IOLoop* loop) {
        std::string port_ = std::to_string(port);
        _ai.init(host,port_,AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        _loop = loop;
        auto on_err = [this](error_c& ec){ on_error(ec,"address resolver");};
        _ai.on_error(on_err);
        _timer.on_error(on_err);
        start_address_resolving();
    }

    void init_resolving_server(int port, IOLoop* loop, const std::string& host_or_interface="") {
        _loop = loop;
        if (host_or_interface.empty()) {
            addrinfo ai;
            memset(&ai, 0, sizeof(ai));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family    = AF_INET; // IPv4 
            addr.sin_addr.s_addr = INADDR_ANY; 
            addr.sin_port = htons(port);
            ai.ai_addr = (sockaddr*)&addr;
            ai.ai_addrlen = sizeof(addr);
            if (_on_resolve) _on_resolve(&ai);
            return;
        }
        if (host_or_interface=="<loopback>") {
            addrinfo ai;
            memset(&ai, 0, sizeof(ai));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family    = AF_INET; // IPv4 
            addr.sin_addr.s_addr = INADDR_LOOPBACK; 
            addr.sin_port = htons(port);
            ai.ai_addr = (sockaddr*)&addr;
            ai.ai_addrlen = sizeof(addr);
            if (_on_resolve) _on_resolve(&ai);
            return;
        }
        ifaddrs *ifaddr;
        errno_c ret = getifaddrs(&ifaddr);
        if (ret) { on_error(ret,"getifaddrs");
        } else {
            addrinfo *info = nullptr;
            addrinfo *ptr = nullptr;
            for (ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL) continue;
                if (host_or_interface==std::string(ifa->ifa_name) 
                    && ifa->ifa_addr->sa_family==AF_INET) {
                        addrinfo* ai = (addrinfo*)alloca(sizeof(addrinfo));
                        if (info==nullptr) { ptr = info = ai;
                            ptr->ai_next = ai;
                            ptr = ai;
                        }
                        ai->ai_addr = ifa->ifa_addr;
                        ai->ai_addrlen = sizeof(sockaddr_in);
                }
            }
            if (info && _on_resolve) _on_resolve(info);
            freeifaddrs(ifaddr);
        }
        init_resolving_client(host_or_interface,port,loop);
    }

    void init_resolving_broadcast(int port, IOLoop* loop, const std::string& interface) {
        _loop = loop;
        if (interface.empty()) {
            addrinfo ai;
            memset(&ai, 0, sizeof(ai));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family    = AF_INET; // IPv4 
            addr.sin_addr.s_addr = INADDR_ANY; 
            addr.sin_port = htons(port);
            ai.ai_addr = (sockaddr*)&addr;
            ai.ai_addrlen = sizeof(addr);
            if (_on_resolve) _on_resolve(&ai);
            return;
        }
        if (interface=="<broadcast>") {
            addrinfo ai;
            memset(&ai, 0, sizeof(ai));
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family    = AF_INET; // IPv4 
            addr.sin_addr.s_addr = INADDR_BROADCAST; 
            addr.sin_port = htons(port);
            ai.ai_addr = (sockaddr*)&addr;
            ai.ai_addrlen = sizeof(addr);
            if (_on_resolve) _on_resolve(&ai);
            return;
        }
        ifaddrs *ifaddr;
        errno_c ret = getifaddrs(&ifaddr);
        if (ret) { on_error(ret,"getifaddrs");
        } else {
            addrinfo *info = nullptr;
            addrinfo *ptr = nullptr;
            for (ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL) continue;
                if (interface==std::string(ifa->ifa_name) 
                    && ifa->ifa_addr->sa_family==AF_INET
                    && ifa->ifa_flags | IFF_BROADCAST) {
                        addrinfo* ai = (addrinfo*)alloca(sizeof(addrinfo));
                        if (info==nullptr) { ptr = info = ai;
                            ptr->ai_next = ai;
                            ptr = ai;
                        }
                        ai->ai_addr = ifa->ifa_broadaddr;
                        ai->ai_addrlen = sizeof(sockaddr_in);
                }
            }
            if (info && _on_resolve) _on_resolve(info);
            else log::error()<<"No broadcast address found on "<<interface<<std::endl;
            freeifaddrs(ifaddr);
        }
    }
    
    void start_address_resolving() {
        error_c ret = _loop->execute(&_ai);
        if (on_error(ret,"address_resolving")) return;
        _ai.on_result_func([this](addrinfo* ai, std::error_code& ec){
            on_addrinfo(ai,ec);
        });
    }

    void on_addrinfo(addrinfo* ai, std::error_code& ec) {
        error_c err(ec.value(),ec.category());
        if (on_error(err,"addrinfo")) {
            _timer.init_oneshoot(5s);
            error_c ret = _loop->execute(&_timer);
            if (on_error(ret,"restart timer")) return;
            _timer.on_shoot_func([this]() { start_address_resolving(); });
            return;
        }
        if (_on_resolve) _on_resolve(ai);
    }

    IOLoop *_loop;
    AddrInfo _ai;
    Timer _timer;
    AddressResolver::callback_t _on_resolve;
};

AddressResolver::AddressResolver():_impl{new AddressResolverImpl{}} {
    auto err = [this](error_c& ec){ on_error(ec,"AddressResolver"); };
    _impl->_ai.on_error(err);
    _impl->_timer.on_error(err);
}
AddressResolver::~AddressResolver() {}

void AddressResolver::init_resolving_client(const std::string& host, int port, IOLoop* loop) {
    _impl->init_resolving_client(host,port,loop);
}

void AddressResolver::init_resolving_server(int port, IOLoop* loop, const std::string& host_or_interface) {
    _impl->init_resolving_server(port,loop,host_or_interface);
}

void AddressResolver::init_resolving_broadcast(int port, IOLoop* loop, const std::string& interface) {
    _impl->init_resolving_broadcast(port,loop,interface);
}

void AddressResolver::on_resolve(AddressResolver::callback_t func) {
    _impl->_on_resolve = func;
}
