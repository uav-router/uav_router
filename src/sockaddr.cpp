#include <avahi-common/address.h>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <linux/if_link.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <utility>
#include "log.h"
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
SockAddr::SockAddr(addrinfo* ai):_impl{new SockAddrImpl{ai->ai_addr,ai->ai_addrlen}} {
    if (ai->ai_canonname) { Log::debug()<<ai->ai_canonname<<std::endl;
    } else {
        Log::debug()<<"Empty canonname"<<std::endl;
    }
}
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

auto SockAddr::init(const std::string& address, uint16_t port) -> error_c {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    _impl->length = 0;
    if (address.empty()) {
        init(INADDR_ANY,port);
        return error_c();
    }
    if (address=="<loopback>") {
        init(INADDR_LOOPBACK,port);
        return error_c();
    }
    if (address=="<broadcast>") {
        init(INADDR_BROADCAST,port);
        return error_c();
    }
    if (address=="<any6>") {
        init(in6addr_any,port);
        return error_c();
    }
    if (address=="<loopback6>") {
        init(in6addr_loopback,port);
        return error_c();
    }
    int ret = inet_pton(AF_INET,address.c_str(),&_impl->addr.in.sin_addr);
    if (ret) {
        _impl->length = sizeof(sockaddr_in);
        _impl->addr.in.sin_family = AF_INET;
        _impl->addr.in.sin_port = htons(port);
        return error_c();
    }
    ret = inet_pton(AF_INET6,address.c_str(),&_impl->addr.in6.sin6_addr);
    if (ret) {
        _impl->length = sizeof(sockaddr_in6);
        _impl->addr.in6.sin6_family = AF_INET6;
        _impl->addr.in6.sin6_port = htons(port);
        return error_c();
    }
    return errno_c();
}

auto SockAddr::sock_addr() -> struct sockaddr* {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    return (struct sockaddr*)&(_impl->addr.storage);
}
auto SockAddr::len() -> socklen_t {
    if (!_impl) return 0;
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

auto SockAddr::family() const -> int {
    if (!_impl) return AF_UNSPEC;
    return _impl->addr.storage.ss_family;
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

auto SockAddr::ip6_addr() -> uint8_t* {
    if (!_impl) return nullptr;
    if (_impl->addr.storage.ss_family!=AF_INET6) return nullptr;
    return _impl->addr.in6.sin6_addr.s6_addr;
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
    return to_errno_c(::bind(fd,sock_addr(),_impl->length),"bind");
}
auto SockAddr::connect(int fd) ->error_c {
    if (!_impl) return errno_c(EINVAL,"connect no address");
    return to_errno_c(::connect(fd,sock_addr(),_impl->length),"bind");
}

auto SockAddr::accept(int fd) ->int {
    if (!_impl) _impl = std::make_unique<SockAddrImpl>();
    return ::accept(fd,sock_addr(),&size());
}

auto SockAddr::to_avahi(AvahiAddress& addr) -> bool {
    if (!_impl) return false;
    addr.proto = avahi_af_to_proto(_impl->addr.storage.ss_family);
    if (addr.proto == AVAHI_PROTO_UNSPEC) return false;
    if (addr.proto == AVAHI_PROTO_INET) {
        addr.data.ipv4.address = _impl->addr.in.sin_addr.s_addr;
    } else {
        memcpy(addr.data.ipv6.address, _impl->addr.in6.sin6_addr.__in6_u.__u6_addr8,sizeof(addr.data.ipv6.address));
    }
    return true;
}

auto SockAddr::itf(bool broadcast) -> std::string {
    if (!_impl) return std::string();
    std::string itf_name;
    ifaddrs *ifaddr;
    error_c ret = to_errno_c(getifaddrs(&ifaddr),"getifaddrs");
    if (ret) return std::string();
    for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_name == nullptr) continue;
        if (ifa->ifa_addr->sa_family!=_impl->addr.storage.ss_family) continue;
        if (broadcast && ifa->ifa_addr->sa_family!=AF_INET && !(ifa->ifa_flags | IFF_BROADCAST)) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            if (broadcast) {
                if (((sockaddr_in*)ifa->ifa_broadaddr)->sin_addr.s_addr == _impl->addr.in.sin_addr.s_addr) {
                    freeifaddrs(ifaddr);
                    return ifa->ifa_name;
                }
            } else if (((sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr == _impl->addr.in.sin_addr.s_addr) {
                freeifaddrs(ifaddr);
                return ifa->ifa_name;
            }
        }
        if (ifa->ifa_addr->sa_family == AF_INET6) {
            if (memcmp(ifa->ifa_addr,&_impl->addr.in6.sin6_addr,sizeof(_impl->addr.in6.sin6_addr))==0) {
                freeifaddrs(ifaddr);
                return ifa->ifa_name;
            }
        }
    }
    freeifaddrs(ifaddr);
    return std::string();
}


auto SockAddr::operator=(SockAddr&& other) noexcept -> SockAddr& {
    if (this != &other) { _impl = std::move(other._impl);
    }
    return *this;
}

auto SockAddr::any(int family, uint16_t port) -> SockAddr {
    SockAddr addr;
    if (family==AF_INET) {         addr.init(INADDR_ANY, port);
    } else if (family==AF_INET6) { addr.init(in6addr_any,port);
    }
    return addr;
}

auto operator<(const SockAddr& addr1, const SockAddr& addr2) -> bool {
    if (!addr1._impl && !addr2._impl) return true;
    if (addr1._impl && addr2._impl) {
        if (addr1._impl->length != addr2._impl->length) return addr1._impl->length < addr2._impl->length;
        auto ret = memcmp(&addr1._impl->addr,&addr2._impl->addr,addr1._impl->length)<0;
        return ret;
    }
    return false;
}

auto SockAddr::format(Format f, const std::string& suffix) -> std::string {
    std::stringstream fmt;
    if (_impl) {
        if (_impl->length) {
            if (_impl->addr.storage.ss_family==AF_INET) {
                std::array<char,INET_ADDRSTRLEN> buf;
                if (f == REG_SERVICE) {
                    fmt<<ntohs(_impl->addr.in.sin_port)<<"-"<<inet_ntop(AF_INET, &_impl->addr.in.sin_addr,buf.data(),buf.size())<<suffix;
                } else if (f == IPADDR_ONLY) {
                    fmt<<inet_ntop(AF_INET, &_impl->addr.in.sin_addr,buf.data(),buf.size());
                }
            } else if (_impl->addr.storage.ss_family==AF_INET6) {
                std::array<char,INET6_ADDRSTRLEN> buf;
                if (f == REG_SERVICE) {
                    fmt<<ntohs(_impl->addr.in6.sin6_port)<<"-"<<inet_ntop(AF_INET6, &_impl->addr.in6.sin6_addr,buf.data(),buf.size())<<suffix;
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

void SockAddrList::add(const SockAddr& addr) {
    push_front(addr);
}
void SockAddrList::add(SockAddr&& addr) {
    push_front(addr);
}

auto SockAddrList::interface(const std::string& name, uint16_t port, int family) -> error_c {
    ifaddrs *ifaddr;
    clear();
    error_c ret = to_errno_c(getifaddrs(&ifaddr),"getifaddrs");
    if (ret) return ret;
    for (ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (family!=AF_UNSPEC && ifa->ifa_addr->sa_family!=family) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_addr->sa_family != AF_INET6) continue;
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
    error_c ret = to_errno_c(getifaddrs(&ifaddr),"getifaddrs");
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
    //Log::debug()<<"ai constructor"<<std::endl;
    for(;ai;ai=ai->ai_next) { add(SockAddr(ai));
    }
}

SockAddrList::SockAddrList(const SockAddr& addr) {
    add(addr);
}

auto itf_from_str(const std::string& name_or_idx) -> std::pair<std::string,int> {
    int itf_idx = if_nametoindex(name_or_idx.c_str());
    if (itf_idx!=0) return std::make_pair(name_or_idx, itf_idx);
    itf_idx = strtol(name_or_idx.c_str(),nullptr,10);
    if (itf_idx==0) return std::make_pair(std::string(), 0);
    std::array<char,IF_NAMESIZE> ifn;
    if (!if_indextoname(itf_idx,ifn.data()))  return std::make_pair(std::string(), 0);
    return std::make_pair(std::string(ifn.data()), itf_idx);
}

auto SockAddr::local(std::string itf_name, int family, uint16_t port) -> SockAddr {
    if (itf_name.empty()) return SockAddr::any(family);
    SockAddrList list;
    list.interface(itf_name,port,family);
    if (list.empty()) return SockAddr::any(family,port);
    if (std::next(list.begin())!=list.end()) return SockAddr::any(family);
    return *list.begin();
}

auto SockAddr::broadcast(std::string itf_name, uint16_t port) -> SockAddr {
    if (itf_name.empty()) return SockAddr::any(AF_INET);
    SockAddrList list;
    list.broadcast(itf_name,port);
    if (list.empty()) return SockAddr::any(AF_INET);
    if (std::next(list.begin())!=list.end()) return SockAddr::any(AF_INET);
    return *list.begin();
}