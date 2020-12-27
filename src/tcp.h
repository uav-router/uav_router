#ifndef __TCP_H__
#define __TCP_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class TcpClient : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& host, uint16_t port, IOLoop* loop) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
    static std::unique_ptr<TcpClient> create(const std::string& name);
};

class TcpSocket : public IOWriteable, public error_handler {
public:
    virtual ~TcpSocket() {};
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
};

class TcpServer : public error_handler {
public:
    using OnConnectFunc = std::function<void(std::unique_ptr<TcpSocket>&, sockaddr*, socklen_t)>;
    virtual void init(uint16_t port, IOLoop* loop, const std::string& host_or_interface="") = 0;
    virtual void on_connect(OnConnectFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
    static std::unique_ptr<TcpServer> create(const std::string& name);
};


#endif //__TCP_H__