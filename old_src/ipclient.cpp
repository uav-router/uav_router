#include <unistd.h>
#include <cstring>
#include <utility>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "err.h"
#include "log.h"
#include "loop.h"
#include "ipclient.h"

#include "addrinfo.h"
static Log::Log log("ipclient");

class IpStream: public IOWriteable, public error_handler {
    virtual void on_connect(OnEventFunc func) = 0;
    virtual void on_read(OnReadFunc func) = 0;
    virtual void on_close(OnEventFunc func) = 0;
};

class IpStreamImpl: public IpStream {
    void on_read(OnReadFunc func) override {
        _on_read = func;
    }
    void on_connect(OnEventFunc func) override {
        _on_connect = func;
        if (_is_connected) _on_connect();
    }
    void on_close(OnEventFunc func) override {
        _on_close = func;
    }
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    OnEventFunc _on_connect;
    bool _is_connected = false;
};


class IpBase : public IOPollable, public IOWriteable, public error_handler {
public:
    IpBase(std::string name):IOPollable(name) {}
    void init(addrinfo* ai) {
        _addr.init(ai);
    }
    void init(SockAddr&& a) {
        _addr.init(std::move(a));
    }
    void on_read(OnReadFunc func) {
        _on_read = func;
    }
    void on_connect(OnEventFunc func) {
        _on_connect = func;
    }
    void on_close(OnEventFunc func) {
        _on_close = func;
    }
    void cleanup() override {
        if (_fd != -1) {
            errno_c ret = err_chk(close(_fd),"close");
            if (ret) on_error(ret);
            _fd = -1;
            is_connected = false;
        }
    }

    auto write(const void* buf, int len) -> int override {
        if (!is_writeable) {
            log.debug()<<"write not writable"<<std::endl;
            return 0;
        }
        int ret = send(buf,len);
        if (ret != len) {
            log.error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }
        return ret;
    }

    /*auto check() -> errno_c {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }*/
    virtual auto check_error() -> bool {return false;}

    auto epollIN() -> int override {
        if (check_error()) return HANDLED;
        while(true) {
            int sz;
            error_c ret = err_chk(ioctl(_fd, FIONREAD, &sz),"ioctl");
            if (ret) { on_error(ret, "Query packet size error");
            } else {
                if (sz==0) {
                    log.debug()<<"nothing to read"<<std::endl;
                    break;
                }
                void* buffer = alloca(sz);
                ssize_t n = receive(buffer, sz);
            }
        }
        return HANDLED;
    }

    auto epollOUT() -> int override {
        is_writeable = true;
        write_allowed();
        if (!is_connected) {
            if (_on_connect) _on_connect();
            is_connected=true;
        }
        return HANDLED;
    }

    auto epollRDHUP() -> int override {
        error_c ret = _loop->del(_fd, this);
        if (ret) { on_error(ret,"tcp event");
        }
        if (_on_close) _on_close();
        cleanup();
        return HANDLED;
    }

    auto epollHUP() -> int override { return epollRDHUP(); }

    auto epollERR() -> int override {
        check_error();
        return HANDLED;
    }

protected:
    SockAddr _addr;
    int _fd = -1;
    bool is_writeable = false;
    bool is_connected = false;
    IOLoop* _loop;
};


class TcpClientImpl : public TcpClient {
public:
    TcpClientImpl(std::string name, IOLoop* loop):_name(std::move(name)),_addr_resolver{AddressResolver::create()},_loop(loop) {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _tcp.on_error(on_err);
        _addr_resolver->on_error(on_err);
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _tcp.init(ai);
            error_c ret = _tcp.start_with(_loop);
            if (ret) { on_error(ret,_name);
            }
        });
        _tcp.on_write_allowed([this](){write_allowed();});
    }
    void init(const std::string& host, uint16_t port) override {
        _addr_resolver->remote(host,port,_loop);
    }

    void init_service(const std::string& service_name, const std::string& interface="") override {
        _query = _loop->query_service(CAvahiService(service_name,"_pktstream._tcp").set_interface(interface).set_ipv4());
        _query->on_failure([this](error_c ec){on_error(ec,_name);});
        _query->on_remove([this](CAvahiService service, AvahiLookupResultFlags flags){
            _tcp.epollRDHUP();
        });
        _query->on_resolve([this](CAvahiService service, std::string host_name,
                                SockAddr addr, std::vector<std::pair<std::string,std::string>> txt,
                                AvahiLookupResultFlags flags){
            _tcp.init(std::move(addr));
            error_c ret = _tcp.start_with(_loop);
            on_error(ret,_name);
        });
    }
    void on_read(OnReadFunc func) override {
        _tcp.on_read(func);
    }

    void on_connect(OnEventFunc func) override {
        _tcp.on_connect(func);
    }

    void on_close(OnEventFunc func) override {
        _tcp.on_close(func);
    }

    auto write(const void* buf, int len) -> int override {
        return _tcp.write(buf,len);
    }

    std::string _name;
    IOLoop *_loop;
    TcpClientBase _tcp;
    std::unique_ptr<AddressResolver> _addr_resolver;
    std::unique_ptr<AvahiQuery> _query;
};

auto TcpClient::create(const std::string& name, IOLoop* loop) -> std::unique_ptr<TcpClient> {
    return std::unique_ptr<TcpClient>{new TcpClientImpl(name,loop)};
}