#ifndef __ADDRINFO_H__
#define __ADDRINFO_H__

#include <memory>
#include "err.h"
#include "epoll.h"

class AddressResolver : public error_handler {
public:
    using callback_t = std::function<void(addrinfo*)>;
    virtual void on_resolve(callback_t func) = 0;

    virtual void init_resolving_client(const std::string& host, uint16_t port, IOLoop* loop)=0;
    virtual void init_resolving_server(uint16_t port, IOLoop* loop, const std::string& host_or_interface="")=0;
    virtual void init_resolving_broadcast(uint16_t port, IOLoop* loop, const std::string& interface="")=0;
    virtual error_c resolve_ip4(const std::string& address, uint16_t port) = 0;
    enum Interface {
        ADDRESS,
        BROADCAST
    };
    virtual error_c resolve_interface_ip4(const std::string& interface, uint16_t port, Interface type) = 0;
    static std::unique_ptr<AddressResolver> create();
};

//TODO: when address resolved on interface or host name the only IPv4 addresses returned. So
//      need to implement IPv6 support

#endif //__ADDRINFO_H__