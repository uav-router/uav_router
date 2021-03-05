#ifndef __IPCLI_H__
#define __IPCLI_H__
#include <memory>

#include "err.h"
#include "loop.h"

class IpClient : public IOWriteable, public error_handler {
public:
    virtual void init_tcp(const std::string& host, uint16_t port) = 0;
    virtual void init_udp(const std::string& host, uint16_t port) = 0;
    virtual void init_service(const std::string& service_name, const std::string& interface="") = 0;
    virtual void init_broadcast(uint16_t port, const std::string& interface="") = 0;
    virtual void init_multicast(const std::string& address, uint16_t port, const std::string& interface="", int ttl = 0) = 0;

    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
    static auto create(const std::string& name, IOLoop* loop) -> std::unique_ptr<IpClient>;
};


#endif //__IPCLI_H__