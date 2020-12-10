#ifndef __UDP_H__
#define __UDP_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class UdpClient : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& host, int port, IOLoop* loop) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    static std::unique_ptr<UdpClient> create(const std::string& name);
};

class UdpServer : public IOWriteable, public error_handler {
public:
    virtual void init(int port, IOLoop* loop, const std::string& host_or_interface="") = 0;
    virtual void on_read(OnReadFunc func) = 0;
    static std::unique_ptr<UdpServer> create(const std::string& name);
};

//TODO: Implement multicasts and broadcasts

#endif //__UDP_H__