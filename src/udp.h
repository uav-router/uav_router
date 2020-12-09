#ifndef __UDP_H__
#define __UDP_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class UdpClient : public IOWriteable, public error_handler {
public:
    UdpClient(const std::string& name);
    ~UdpClient();
    void on_read_func(OnReadFunc func);
    void on_connect_func(OnEventFunc func);
    void init(const std::string& host, int port, IOLoop* loop);
    int write(const void* buf, int len) override;
private:
    class UdpClientImpl;
    std::unique_ptr<UdpClientImpl> _impl;
};

class UdpServer : public IOWriteable, public error_handler {
public:
    UdpServer(const std::string& name);
    ~UdpServer();
    void on_read_func(OnReadFunc func);
    void init(int port, IOLoop* loop, const std::string& host_or_interface="");
    int write(const void* buf, int len) override;
private:
    class UdpServerImpl;
    std::unique_ptr<UdpServerImpl> _impl;
};

//TODO: Implement multicasts and broadcasts

#endif //__UDP_H__