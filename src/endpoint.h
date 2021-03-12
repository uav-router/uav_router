#ifndef __ENDPOINT_H__
#define __ENDPOINT_H__
#include <memory>
#include <string>
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

class IOLoop {
public:
    // loop items
    //virtual auto uart(const std::string& name) -> std::unique_ptr<UART> = 0;
    //virtual auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> = 0;
    //virtual auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> = 0;
    //virtual auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> = 0;
    //virtual auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> = 0;
    //virtual auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> = 0;
    // stats
    //virtual auto stats() -> StatHandler& = 0;
    // run
    virtual auto run() -> int = 0;
    virtual void stop() = 0;
    //virtual auto handle_CtrlC() -> error_c = 0;

    //virtual auto handle_udev() -> error_c = 0;
    //virtual auto handle_zeroconf() -> error_c = 0;
    static auto loop() -> std::unique_ptr<IOLoop>;
};


#endif //__ENDPOINT_H__

