#ifndef __ADDRINFO_H__
#define __ADDRINFO_H__

#include <memory>
#include "err.h"
#include "epoll.h"

class AddressResolver : public error_handler {
public:
    using callback_t = std::function<void(addrinfo*)>;
    
    AddressResolver();
    ~AddressResolver();
    void init_resolving_client(const std::string& host, int port, IOLoop* loop);
    void init_resolving_server(int port, IOLoop* loop, const std::string& host_or_interface="");
    void on_resolve(callback_t func);
private:
    class AddressResolverImpl;
    std::unique_ptr<AddressResolverImpl> _impl;
};


#endif //__ADDRINFO_H__