#ifndef __UART_H__
#define __UART_H__
#include <memory>

#include "err.h"
#include "loop.h"

class UART : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& path, IOLoop* loop, int baudrate=115200, bool flow_control=false) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
    static auto create(const std::string& name) -> std::unique_ptr<UART>;
};

#endif //__UART_H__