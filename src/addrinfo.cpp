#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ifaddrs.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/ioctl.h>

#include <linux/if_link.h>
#include <net/if.h>
#include <arpa/inet.h>

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
    void on_result(addrinfo* ai, std::error_code& ec);
private:
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



AddrInfo::AddrInfo():IOPollable("addrinfo") {
    _hints.ai_family = AF_UNSPEC;
    _hints.ai_socktype = 0;
    _hints.ai_protocol = 0;
    _hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
}

AddrInfo::~AddrInfo() {}

void AddrInfo::init(const std::string& name, const std::string& port, 
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

void AddrInfo::on_result(addrinfo* ai, std::error_code& ec) {
    if (_on_result) _on_result(ai,ec);
}

void AddrInfo::on_result_func(callback_t func) {
    _on_result = func;
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
    _sfd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (_sfd == -1) {
        return errno_c("signalfd");
    }
    _loop = loop;
    ret = _loop->add(_sfd, EPOLLIN, this);
    if (ret) {
        ret.add_place("IOLoop add");
        close(_sfd);
        return ret;
    }
    se.sigev_value.sival_ptr = this;
    gaicb * ptr = &req;
    eai_code ec = getaddrinfo_a(GAI_NOWAIT, &ptr, 1, &se);
    if (on_error(ec, "getaddrinfo")) {
        ret = _loop->del(_sfd, this);
        on_error(ret,"IOLoop del");
        close(_sfd);
        return ec;
    }
    log::debug()<<"start_with ends"<<std::endl;
    return error_c();
}

int AddrInfo::epollIN() {
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
        error_c ec = eai_code(&req);
        on_result(req.ar_result, ec);
        if (req.ar_result) { freeaddrinfo(req.ar_result);
        }
        _loop->del(_sfd, this);
        cleanup();
        break;
    }
    return HANDLED;
}

void AddrInfo::cleanup() {
    close(_sfd);
    log::debug()<<"Cleanup called"<<std::endl;
}

class AddressResolverImpl : public AddressResolver {
public:

    void on_resolve(callback_t func) override {
        _on_resolve = func;
    }

    void init_resolving_client(const std::string& host, uint16_t port, IOLoop* loop) override {
        std::string port_ = std::to_string(port);
        _ai.init(host,port_,AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        _loop = loop;
        auto on_err = [this](error_c& ec){ on_error(ec,"address resolver");};
        _ai.on_error(on_err);
        _timer.on_error(on_err);
        start_address_resolving();
    }

    void resolve_ipv4(in_addr_t address, uint16_t port) {
        addrinfo ai;
        memset(&ai, 0, sizeof(ai));
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family    = AF_INET; // IPv4 
        addr.sin_addr.s_addr = address; 
        addr.sin_port = htons(port);
        ai.ai_addr = (sockaddr*)&addr;
        ai.ai_addrlen = sizeof(addr);
        if (_on_resolve) _on_resolve(&ai);
    }

    error_c resolve_ip4(const std::string& address, uint16_t port) override {
        in_addr addr;
        int ret = inet_pton(AF_INET,address.c_str(),&addr);
        if (ret==1) {
            resolve_ipv4(addr.s_addr,port);
            return error_c();
        }
        if (ret==0) return errno_c(EINVAL,"inet_pton"); //address is not like XXX.XXX.XXX
        return errno_c("inet_pton");
    }

    error_c resolve_interface_ip4(const std::string& interface, uint16_t port, Interface type) override {
        ifaddrs *ifaddr;
        error_c ret = err_chk(getifaddrs(&ifaddr),"getifaddrs");
        if (ret) return ret;

        addrinfo *info = nullptr;
        addrinfo *ptr = nullptr;
        for (ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (type==Interface::BROADCAST && !(ifa->ifa_flags | IFF_BROADCAST)) continue;
            if (ifa->ifa_addr == NULL) continue;
            if (interface==std::string(ifa->ifa_name)) {
                addrinfo* ai = (addrinfo*)alloca(sizeof(addrinfo));
                if (info==nullptr) { ptr = info = ai;
                    ptr->ai_next = ai;
                    ptr = ai;
                }
                ai->ai_addrlen = sizeof(sockaddr_in);
                switch(type) {
                    case Interface::ADDRESS:   ai->ai_addr = ifa->ifa_addr;      break;
                    case Interface::BROADCAST: ai->ai_addr = ifa->ifa_broadaddr; break;
                }
            }
        }
        if (info) { 
            if (_on_resolve) _on_resolve(info);
            freeifaddrs(ifaddr);
            return error_c();
        }
        return error_c(ENODATA);
    }

    void init_resolving_server(uint16_t port, IOLoop* loop, const std::string& host_or_interface="") override {
        _loop = loop;
        if (host_or_interface.empty()) {
            resolve_ipv4(INADDR_ANY, port);
            return;
        }
        if (host_or_interface=="<loopback>") {
            resolve_ipv4(INADDR_LOOPBACK, port);
            return;
        }
        error_c ret = resolve_interface_ip4(host_or_interface, port, Interface::ADDRESS);
        if (ret) {
            init_resolving_client(host_or_interface,port,loop);
        }
    }

    void init_resolving_broadcast(uint16_t port, IOLoop* loop, const std::string& interface) override {
        _loop = loop;
        if (interface.empty()) {
            resolve_ipv4(INADDR_ANY, port);
            return;
        }
        if (interface=="<broadcast>") {
            resolve_ipv4(INADDR_BROADCAST, port);
            return;
        }
        error_c ret = resolve_interface_ip4(interface, port, Interface::BROADCAST);
        if (ret) {
            log::error()<<"No broadcast address found on "<<interface<<std::endl;
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
    callback_t _on_resolve;
};

std::unique_ptr<AddressResolver> AddressResolver::create() {
    return std::unique_ptr<AddressResolver>{new AddressResolverImpl()};
}

