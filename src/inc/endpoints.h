#ifndef __ENDPOINTS_H__
#define __ENDPOINTS_H__
#include <functional>
#include <memory>
#include <sys/socket.h>

#include "../err.h"
#include "stat.h"
/*
               UART  TCP_SRV TCPSRV_STREAM TCP_CLI UDP_SRV UDP_SRV_STREAM UDP_CLI
on_read          X      0          X          X       0          X           X
on_write         X      0          X          X       X          0           X
on_connect
(on_name)        X      0          0          X       0          0           X
on_connect_cli   0      X          0          0       X          0           0
on_close         X      X          X          X       X          X           X
*/
using OnEventFunc = std::function<void()>;

class Readable : public error_handler {
public:
    using OnReadFunc  = std::function<void(void*, int)>;
    void on_read(OnReadFunc func) {_on_read = func;}
    virtual auto get_peer_name() -> const std::string& = 0;
protected:
    virtual void on_read(void* buf, int len) { if (_on_read) _on_read(buf, len); }
private:
    OnReadFunc _on_read;
};

class Writeable {
public:
    virtual ~Writeable() = default;
    virtual auto write(const void* buf, int len) -> int = 0;
    void writeable(OnEventFunc func) {_writeable = func;}
    auto is_writeable() -> bool { return _is_writeable; }
protected:
    void writeable() {
        if (_is_writeable) return;
        _is_writeable = true;
        if (_writeable) _writeable(); 
    }
    bool _is_writeable = false;
private:
    OnEventFunc _writeable;
};

class Closeable {
public:
    virtual ~Closeable() = default;
    void on_close(OnEventFunc func) {_on_close = func;}
protected:
    virtual void on_close() { if (_on_close) _on_close(); }
private:
    OnEventFunc _on_close;
};

class Client : public Readable, public Writeable, public Closeable {
};

class StreamSource: public error_handler {
public:
    using OnConnectFunc  = std::function<void(std::shared_ptr<Client>, std::string)>;
    void on_connect(OnConnectFunc func) { _on_connect = func;
    }
protected:
    void on_connect(std::shared_ptr<Client> cli, std::string name) { 
        if (_on_connect) _on_connect(std::move(cli), std::move(name)); 
    }
private:
    OnConnectFunc _on_connect;
};

class UART: public StreamSource {
public:
    virtual auto init(const std::string& path, int baudrate=115200, bool flow_control=false) -> error_c = 0;
};

class TcpClient: public StreamSource {
public:
    virtual auto init(const std::string& host, uint16_t port, int family=AF_UNSPEC) -> error_c = 0;
    virtual auto init_service(const std::string& service_name, const std::string& interface="") -> error_c = 0;
};

class UdpClient: public StreamSource, public Writeable {
public:
    virtual auto init(const std::string& host, uint16_t port, int family=AF_UNSPEC) -> error_c = 0;
    virtual auto init_broadcast(uint16_t port, const std::string& interface="") -> error_c = 0;
    virtual auto init_multicast(const std::string& address, uint16_t port, const std::string& interface="", uint8_t ttl = 0) -> error_c = 0;
    virtual auto init_service(const std::string& service_name, const std::string& interface="") -> error_c = 0;
};

class TcpServer:  public StreamSource {
public:
    virtual auto init(uint16_t port = 0, int family = AF_INET) -> error_c = 0;
    
    virtual auto address(const std::string& address) -> TcpServer& = 0;
    virtual auto interface(const std::string& interface, int family = AF_INET) -> TcpServer& = 0;
};

class UdpServer:  public StreamSource {
public:
    enum Mode { UNICAST, BROADCAST, MULTICAST };
    virtual auto address(const std::string& address) -> UdpServer& = 0;
    virtual auto interface(const std::string& interface) -> UdpServer& = 0;
    virtual auto service_port_range(uint16_t min, uint16_t max) -> UdpServer& = 0;
    virtual auto ttl(uint8_t ttl_) -> UdpServer& = 0;

    virtual auto init(uint16_t port=0, Mode mode = UNICAST) -> error_c = 0;
};

class Endpoint : public error_handler, public Writeable {
public:
    virtual auto stat() -> std::shared_ptr<Stat> = 0;
};

class Pipe : public Endpoint {
public:
    void chain(std::shared_ptr<Writeable> next) { _next = next;
    }
protected:
    auto write_next(const void* buf, int len) -> int {
        if (!_next.expired())  return _next.lock()->write(buf,len);
        return 0;
    }
    std::weak_ptr<Writeable> _next;
};

class Filter : public Pipe {
public:
    void rest(std::shared_ptr<Writeable> r) { _rest = r;
    }
protected:
    auto write_rest(const void* buf, int len) -> int {
        if (!_rest.expired())  return _rest.lock()->write(buf,len);
        return 0;
    }
    std::weak_ptr<Writeable> _rest;
};

#endif //__ENDPOINTS_H__