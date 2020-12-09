#ifndef __TCP_H__
#define __TCP_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class TcpClient : public IOWriteable, public error_handler {
public:
    TcpClient(const std::string& name);
    ~TcpClient();
    void init(const std::string& host, int port, IOLoop* loop);
    int write(const void* buf, int len) override;
    void on_read_func(OnReadFunc func);
    void on_connect_func(OnEventFunc func);
    void on_close_func(OnEventFunc func);
private:
    class TcpClientImpl;
    std::unique_ptr<TcpClientImpl> _impl;
};

#endif //__TCP_H__