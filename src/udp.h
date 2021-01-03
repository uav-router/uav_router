#ifndef __UDP_H__
#define __UDP_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class UdpClient : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& host, uint16_t port, IOLoop* loop) = 0;
    virtual void init_broadcast(uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface="", int ttl = 0) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    static auto create(const std::string& name) -> std::unique_ptr<UdpClient>;
};

class UdpServer : public IOWriteable, public error_handler {
public:
    virtual void init(uint16_t port, IOLoop* loop, const std::string& host="") = 0;
    virtual void init_interface(const std::string& interface, uint16_t port, IOLoop* loop) = 0;
    virtual void init_broadcast(uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void on_read(OnReadFunc func) = 0;
    static auto create(const std::string& name) -> std::unique_ptr<UdpServer>;
};

#endif //__UDP_H__