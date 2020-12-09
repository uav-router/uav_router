#ifndef __ADDRINFO_H__
#define __ADDRINFO_H__

#include <memory>
#include "err.h"
#include "epoll.h"

class AddrInfo : public IOPollable {
public:
    using callback_t = std::function<void(addrinfo*, std::error_code& ec)>;
    AddrInfo();
    ~AddrInfo();
    void init(const std::string& name, const std::string& port="", 
             int family = AF_UNSPEC, int socktype = 0, int protocol = 0, 
             int flags = AI_V4MAPPED | AI_ADDRCONFIG);
    void on_result_func(callback_t func);
    error_c start_with(IOLoop* loop) override;
    void events(IOLoop* loop, uint32_t evs) override;
    void cleanup() override;
private:
    class AddrInfoImpl;
    std::unique_ptr<AddrInfoImpl> _impl;
};

class AddressResolver : public error_handler {
public:
    using callback_t = std::function<void(addrinfo*)>;
    
    AddressResolver();
    ~AddressResolver();
    void init_resolving_client(const std::string& host, int port, IOLoop* loop);
    void init_resolving_server(int port, IOLoop* loop, const std::string& host_or_interface="");
    void on_resolve_func(callback_t func);
private:
    class AddressResolverImpl;
    std::unique_ptr<AddressResolverImpl> _impl;
};


#endif //__ADDRINFO_H__