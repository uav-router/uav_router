#ifndef __ENDPOINT_H__
#define __ENDPOINT_H__
#include <memory>
#include <string>
#include <sys/signalfd.h>
#include "err.h"
#include "measure.h"

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
    virtual void on_read(OnReadFunc func) = 0;
    virtual auto get_peer_name() -> const std::string& = 0;
};

class Writeable {
public:
    virtual ~Writeable() = default;
    virtual auto write(const void* buf, int len) -> int = 0;
    void writeable(OnEventFunc func) {_writeable = func;}
    auto is_writeable() -> bool { return _is_writeable; }
protected:
    void writeable() {if (_writeable) _writeable(); _is_writeable = true;}
    bool _is_writeable = false;
private:
    OnEventFunc _writeable;
};

class Closeable {
public:
    virtual ~Closeable() = default;
    virtual void on_close(OnEventFunc func) = 0;
protected:
    void on_close() { if (_on_close) _on_close(); }
private:
    OnEventFunc _on_close;
};

class Client : public Readable, public Writeable, public Closeable {
};

class StreamSource: public error_handler {
public:
    using OnConnectFunc  = std::function<void(std::unique_ptr<Client>, std::string)>;
    virtual void on_connect(OnConnectFunc func) = 0;
protected:
    void on_connect(std::unique_ptr<Client> cli, std::string name) { 
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
    virtual auto init(const std::string& host, uint16_t port) -> error_c = 0;
};

class ServiceClient: public StreamSource {
public:
    virtual auto init(const std::string& service_name, const std::string& interface="") -> error_c = 0;
};

class UdpClient: public StreamSource {
public:
    virtual auto init(const std::string& host, uint16_t port) -> error_c = 0;
    virtual auto init_broadcast(uint16_t port, const std::string& interface="") -> error_c = 0;
    virtual auto init_multicast(const std::string& address, uint16_t port, const std::string& interface="", int ttl = 0) -> error_c = 0;
};

class TcpServer:  public StreamSource {
public:
    virtual auto init(uint16_t port) -> error_c = 0;
    virtual auto init_service(const std::string& service_name) -> error_c = 0;
    virtual auto address(const std::string& address) -> TcpServer& = 0;
    virtual auto interface(const std::string& address) -> TcpServer& = 0;
};

class UdpServer:  public StreamSource {
public:
    virtual auto interface(const std::string& interface) -> UdpServer& = 0;
    virtual auto service_port_range(uint16_t port_min, uint16_t port_max) -> UdpServer& = 0;
    virtual auto ttl(int ttl_) -> TcpServer& = 0;
    virtual auto address(const std::string& address) -> TcpServer& = 0;
    virtual auto broadcast() -> TcpServer& = 0;
    virtual auto multicast() -> TcpServer& = 0;

    virtual auto init(uint16_t port) -> error_c = 0;
    virtual auto init_service(const std::string& service_name) -> error_c = 0;
};

class Signal : public error_handler {
public:
    using OnSignalFunc  = std::function<bool(signalfd_siginfo*)>;
    virtual auto init(std::initializer_list<int> signals, OnSignalFunc handler) -> error_c = 0;
};

class IOPollable;
class Poll {
public:
    virtual auto add(int fd, uint32_t events, IOPollable* obj) -> errno_c = 0;
    virtual auto mod(int fd, uint32_t events, IOPollable* obj) -> errno_c = 0;
    virtual auto del(int fd, IOPollable* obj) -> errno_c = 0;
};

class IOPollable {
public:
    enum {
        NOT_HANDLED = 0,
        HANDLED,
        STOP
    };
    IOPollable(std::string  n):name(std::move(n)) {}
    virtual ~IOPollable() = default;
    virtual auto epollEvent(int /*event*/) -> bool { return false; }
    virtual auto epollIN() -> int { return NOT_HANDLED; }
    virtual auto epollOUT() -> int { return NOT_HANDLED; }
    virtual auto epollPRI() -> int { return NOT_HANDLED; }
    virtual auto epollERR() -> int { return NOT_HANDLED; }
    virtual auto epollRDHUP() -> int { return NOT_HANDLED; }
    virtual auto epollHUP() -> int { return NOT_HANDLED; }
    virtual void udev_add(const std::string& node, const std::string& id) {};
    virtual void udev_remove(const std::string& node, const std::string& id) {};
    virtual auto start_with(Poll* poll) -> error_c {return error_c(ENOTSUP);}
    virtual void cleanup() {}

    std::string name;
};

class IOLoop  : public error_handler {
public:
    // loop items
    //virtual auto uart(const std::string& name) -> std::unique_ptr<UART> = 0;
    //virtual auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> = 0;
    //virtual auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> = 0;
    //virtual auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> = 0;
    //virtual auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> = 0;
    //virtual auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> = 0;
    virtual auto signal_handler() -> std::unique_ptr<Signal> = 0;
    // stats
    //virtual auto stats() -> StatHandler& = 0;
    // run
    virtual void run()  = 0;
    virtual void stop() = 0;
    virtual auto poll() -> Poll* = 0;
    virtual auto handle_CtrlC() -> error_c = 0;

    //virtual auto handle_udev() -> error_c = 0;
    //virtual auto handle_zeroconf() -> error_c = 0;
    static auto loop(int pool_events=5) -> std::unique_ptr<IOLoop>;
};


#endif //__ENDPOINT_H__

