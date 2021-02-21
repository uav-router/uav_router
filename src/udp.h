#ifndef __UDP_H__
#define __UDP_H__
#include <memory>

#include "err.h"
#include "loop.h"

/*

[tcp[6]://|udp[6]://]address:port - general address
[tcp[6]://|udp[6]://]:port[/interface_name] - address of the local interface
[tcp[6]://|udp[6]://]name/interface_name - service declaration on specific interface

bcast[6]://:port[/interface_name] - broadcast address (of specific interface)
bcast[6]://name[:port_min-port_max][/interface_name] - broadcast service on specific interface

mcast[6]://address:port[/interface_name] - multicast address (of specific interface)
mcast[6]://name[:port_min-port_max][/interface_name] - multicast service on specific interface
*/
class UdpClient : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& host, uint16_t port, IOLoop* loop) = 0;
    virtual void init_service(const std::string& service_name, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_broadcast(uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface="", int ttl = 0) = 0;

    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_connect(OnEventFunc func) = 0;
    static auto create(const std::string& name) -> std::unique_ptr<UdpClient>;
};

class UdpStream: public IOWriteable {
public:
  virtual void on_read(OnReadFunc func) = 0;
};


class UdpServer : public error_handler {
public:
    using OnConnectFunc = std::function<void(std::shared_ptr<UdpStream>&)>;
    virtual void init(uint16_t port, IOLoop* loop, const std::string& host="") = 0;
    virtual void init_interface(const std::string& interface, uint16_t port, IOLoop* loop) = 0;
    virtual void init_service(const std::string& service_name, const std::string& interface, IOLoop* loop) = 0;
    virtual void init_broadcast(uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_broadcast_service(const std::string& service_name, const std::string& interface, IOLoop* loop) = 0;
    virtual void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface="") = 0;
    virtual void init_multicast_service(const std::string& service_name, const std::string& interface, IOLoop* loop) = 0;
    virtual void on_connect(OnConnectFunc func) = 0;
    static auto create(const std::string& name) -> std::unique_ptr<UdpServer>;
};

#endif //__UDP_H__