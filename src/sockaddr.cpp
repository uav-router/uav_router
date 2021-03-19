#include <cstring>
#include <sstream>
#include <linux/if_link.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include "sockaddr.h"

class SockAddr::SockAddrImpl {
public:
    SockAddrImpl() = default;
    SockAddrImpl(struct sockaddr* a, socklen_t l):length(l) {
        memcpy(&(addr.storage),a,l);
    }
    void init(struct sockaddr* a, socklen_t l) {
        length = l;
        memcpy(&(addr.storage),a,l);
    }
    void init(addrinfo* ai) {
        init(ai->ai_addr,ai->ai_addrlen);
    }
    void init(int fd) {
        length=sizeof(addr);
        int ret = getsockname(fd,(sockaddr*)&addr.storage, &length);
        if (ret) { length = 0;
        }
    }
    void init(const AvahiAddress * a, uint16_t port) {
        memset(&addr, 0, sizeof(addr));
        length = 0;
        addr.storage.ss_family = avahi_proto_to_af(a->proto);
        if (addr.storage.ss_family == AF_INET) {
            addr.in.sin_addr.s_addr = a->data.ipv4.address;
            addr.in.sin_port = htons(port);
            length = sizeof(sockaddr_in);
        } else if (addr.storage.ss_family == AF_INET6) {
            memcpy(&addr.in6.sin6_addr.__in6_u.__u6_addr8, a->data.ipv6.address, sizeof(a->data.ipv6.address));
            addr.in6.sin6_port = htons(port);
            length = sizeof(sockaddr_in6);
        }
    }

    union {
        sockaddr_storage storage;
        sockaddr_in in;
        sockaddr_in6 in6;
        sockaddr_nl nl;
    } addr;
    socklen_t length = 0;
};

SockAddr::SockAddr() = default;
SockAddr::~SockAddr() = default;
SockAddr::SockAddr(struct sockaddr* addr, socklen_t len):_impl{new SockAddrImpl{addr,len}} {}
SockAddr::SockAddr(addrinfo* ai):_impl{new SockAddrImpl{ai->ai_addr,ai->ai_addrlen}} {}
SockAddr::SockAddr(SockAddr&& addr):_impl(std::move(addr._impl)) {}
SockAddr::SockAddr(in_addr_t address, uint16_t port):_impl{new SockAddrImpl{}} {
    _impl->addr.in.sin_family    = AF_INET;
    _impl->addr.in.sin_addr.s_addr = address;
    _impl->addr.in.sin_port = htons(port);
    _impl->length = sizeof(sockaddr_in);
}
SockAddr::SockAddr(int fd):_impl{new SockAddrImpl{}} {
    _impl->init(fd);
}
SockAddr::SockAddr(const AvahiAddress * addr, uint16_t port):_impl{new SockAddrImpl{}} {
    _impl->init(addr,port);
}

SockAddr::SockAddr(const SockAddr& addr) {
    if (!addr._impl) return;
    _impl = std::make_unique<SockAddrImpl>((struct sockaddr*)&(addr._impl->addr.storage),addr._impl->length);
}
void SockAddr::init(struct sockaddr *addr, socklen_t len) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->init(addr,len);
}
void SockAddr::init(addrinfo *ai) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->init(ai);
}

void SockAddr::init(int fd) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->init(fd);
}

void SockAddr::init(SockAddr&& addr) {
    _impl = std::move(addr._impl);
}
    

void SockAddr::init(const AvahiAddress * addr, uint16_t port) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->init(addr,port);
}


void SockAddr::init(in_addr_t address, uint16_t port) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->addr.in.sin_family    = AF_INET;
    _impl->addr.in.sin_addr.s_addr = address;
    _impl->addr.in.sin_port = htons(port);
    _impl->length = sizeof(sockaddr_in);
}

void SockAddr::init(in6_addr address, uint16_t port) {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->addr.in6.sin6_family = AF_INET6;
    _impl->addr.in6.sin6_flowinfo = 0;
    _impl->addr.in6.sin6_port = htons(port);
    _impl->addr.in6.sin6_addr = address;
    _impl->length = sizeof(sockaddr_in6);
}

auto SockAddr::init(const std::string& address, uint16_t port) -> bool {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->length = 0;
    if (address.empty()) {
        init(INADDR_ANY,port);
        return true;
    }
    if (address=="<loopback>") {
        init(INADDR_LOOPBACK,port);
        return true;
    }
    if (address=="<broadcast>") {
        init(INADDR_BROADCAST,port);
        return true;
    }
    if (address=="<any6>") {
        init(in6addr_any,port);
        return true;
    }
    if (address=="<loopback6>") {
        init(in6addr_loopback,port);
        return true;
    }
    int ret = inet_pton(AF_INET,address.c_str(),&_impl->addr.in.sin_addr);
    if (ret) {
        _impl->length = sizeof(sockaddr_in);
        _impl->addr.in.sin_family = AF_INET;
        _impl->addr.in.sin_port = htons(port);
        return true;
    }
    ret = inet_pton(AF_INET6,address.c_str(),&_impl->addr.in6.sin6_addr);
    if (ret) {
        _impl->length = sizeof(sockaddr_in6);
        _impl->addr.in6.sin6_family = AF_INET6;
        _impl->addr.in6.sin6_port = htons(port);
        return true;
    }
    return false;
}

auto SockAddr::sock_addr() -> struct sockaddr* {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    return (struct sockaddr*)&(_impl->addr.storage);
}
auto SockAddr::len() -> socklen_t {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    return _impl->length;
}

auto SockAddr::size() -> socklen_t& {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->length = sizeof(_impl->addr);
    return _impl->length;
}

auto SockAddr::is_ip4() -> bool {
    if (!_impl) return false;
    return _impl->addr.storage.ss_family==AF_INET;
}

auto SockAddr::is_any() -> bool {
    if (!_impl) return false;
    return _impl->addr.storage.ss_family==AF_INET && _impl->addr.in.sin_addr.s_addr==htonl(INADDR_ANY);
}

auto SockAddr::ip4_addr_t() -> in_addr_t {
    if (!_impl) return htonl(INADDR_ANY);
    if (_impl->addr.storage.ss_family!=AF_INET) return htonl(INADDR_ANY);
    return _impl->addr.in.sin_addr.s_addr;
}

auto SockAddr::port() -> uint16_t {
    if (!_impl) return 0;
    if (_impl->addr.storage.ss_family==AF_INET) return ntohs(_impl->addr.in.sin_port);
    if (_impl->addr.storage.ss_family==AF_INET6) return ntohs(_impl->addr.in6.sin6_port);
    return 0;
}

void SockAddr::set_port(uint16_t port) {
    if (!_impl) return;
    if (_impl->addr.storage.ss_family==AF_INET) _impl->addr.in.sin_port = htons(port);
    if (_impl->addr.storage.ss_family==AF_INET6) _impl->addr.in6.sin6_port = htons(port);
}

auto SockAddr::operator=(const SockAddr& other) -> SockAddr& {
    if (this != &other) {
        if (!_impl) _impl = std::make_unique<SockAddrImpl>();
        memcpy(&_impl->addr,&other._impl->addr, other._impl->length);
        _impl->length = other._impl->length;
    }
    return *this;
}

auto SockAddr::bind(int fd) ->error_c {
    if (!_impl) return errno_c(EINVAL,"bind no address");
    return err_chk(::bind(fd,(sockaddr*)&(_impl->addr.storage),_impl->length),"bind");
}
auto SockAddr::connect(int fd) ->error_c {
    if (!_impl) return errno_c(EINVAL,"connect no address");
    return err_chk(::connect(fd,(sockaddr*)&(_impl->addr.storage),_impl->length),"bind");
}

auto SockAddr::operator=(SockAddr&& other) noexcept -> SockAddr& {
    if (this != &other) { _impl = std::move(other._impl);
    }
    return *this;
}

auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool {
    if (!addr1._impl && !addr2._impl) return true;
    if (addr1._impl && addr2._impl) {
        if (addr1._impl->length != addr2._impl->length) return addr1._impl->length < addr2._impl->length;
        return memcmp(&addr1._impl->addr,&addr2._impl->addr,addr1._impl->length)<0;
    }
    return bool(addr1._impl);
}

auto SockAddr::format(Format f) -> std::string {
    std::stringstream fmt;
    if (_impl) {
        if (_impl->length) {
            if (_impl->addr.storage.ss_family==AF_INET) {
                std::array<char,256> buf;
                if (f == REG_SERVICE) {
                    fmt<<ntohs(_impl->addr.in.sin_port)<<"."<<inet_ntop(AF_INET, &_impl->addr.in.sin_addr,buf.data(),buf.size());
                } else if (f == IPADDR_ONLY) {
                    fmt<<inet_ntop(AF_INET, &_impl->addr.in.sin_addr,buf.data(),buf.size());
                }
            } else if (_impl->addr.storage.ss_family==AF_INET6) {
                std::array<char,256> buf;
                if (f == REG_SERVICE) {
                    fmt<<ntohs(_impl->addr.in6.sin6_port)<<"."<<inet_ntop(AF_INET6, &_impl->addr.in6.sin6_addr,buf.data(),buf.size());
                } else if (f == IPADDR_ONLY) {
                    fmt<<inet_ntop(AF_INET6, &_impl->addr.in6.sin6_addr,buf.data(),buf.size());
                }
            }
        }
    }
    return fmt.str();
}

auto operator<<(std::ostream &os, const SockAddr &addr) -> std::ostream& {
    if (!addr._impl) {
        os<<"empty address";
    } else {
        if (addr._impl->length==0) {
            os<<"0-length address";
        } else if (addr._impl->addr.storage.ss_family==AF_INET) {
            std::array<char,256> buf;
            os<<inet_ntop(AF_INET, &addr._impl->addr.in.sin_addr,buf.data(),buf.size())<<":"<<ntohs(addr._impl->addr.in.sin_port);
        } else if (addr._impl->addr.storage.ss_family==AF_INET6) {
            std::array<char,256> buf;
            os<<inet_ntop(AF_INET6, &addr._impl->addr.in6.sin6_addr,buf.data(),buf.size())<<":"<<ntohs(addr._impl->addr.in6.sin6_port);
        } else {
            os<<"unknown address of length "<<addr._impl->length;
        }
    }
    return os;
}

auto SockAddrList::current() -> SockAddr& {
    if (_current==end()) _current = begin();
    return *_current;
}
auto SockAddrList::next() -> SockAddr& {
    if (_current==end()) { _current = begin(); return *_current;
    }
    _current++;
    if (_current==end()) _current = begin();
    return *_current;
}
void SockAddrList::add(const SockAddr& addr) {
    push_front(addr);
    _current = begin();
}
void SockAddrList::add(SockAddr&& addr) {
    push_front(addr);
    _current = begin();
}

auto SockAddrList::interface(const std::string& name, uint16_t port, int family) -> error_c {
    ifaddrs *ifaddr;
    clear();
    error_c ret = err_chk(getifaddrs(&ifaddr),"getifaddrs");
    if (ret) return ret;
    for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (family!=AF_UNSPEC && ifa->ifa_addr->sa_family!=family) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_addr->sa_family != AF_INET6) continue;
        if (ifa->ifa_addr == nullptr) continue;
        if (name==std::string(ifa->ifa_name)) {
            add(SockAddr(ifa->ifa_addr,ifa->ifa_addr->sa_family == AF_INET ? sizeof(sockaddr_in):sizeof(sockaddr_in6)));
        }
    }
    freeifaddrs(ifaddr);
    return empty()?error_c(ENODATA):error_c();
}

auto SockAddrList::broadcast(const std::string& name, uint16_t port) -> error_c {
    ifaddrs *ifaddr;
    clear();
    error_c ret = err_chk(getifaddrs(&ifaddr),"getifaddrs");
    if (ret) return ret;
    for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags | IFF_BROADCAST)) continue;
        if (ifa->ifa_addr == nullptr) continue;
        if (name==std::string(ifa->ifa_name)) {
            add(SockAddr(ifa->ifa_broadaddr,sizeof(sockaddr_in)));
        }
    }
    freeifaddrs(ifaddr);
    return empty()?error_c(ENODATA):error_c();
}

SockAddrList::SockAddrList(addrinfo *ai) {
    for(;ai;ai=ai->ai_next) { add(SockAddr(ai));
    }
}
