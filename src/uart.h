#ifndef __UART_H__
#define __UART_H__
#include <memory>

#include "err.h"
#include "epoll.h"

class UART : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& path, int baudrate, bool flow_control, IOLoop* loop) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
    static std::unique_ptr<UART> create(const std::string& name);
};

#endif //__UART_H__