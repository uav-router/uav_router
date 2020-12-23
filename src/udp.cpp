#include <unistd.h>
//#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>


#include "err.h"
#include "log.h"
#include "epoll.h"
#include "udp.h"

#include "addrinfo.h"
//#include "timer.h"

class UdpBase : public IOPollable, public IOWriteable, public error_handler {
public:
    UdpBase(const std::string n):IOPollable(n) {}
    void init(socklen_t addrlen, sockaddr *addr, bool broadcast=false) {
        _broadcast=broadcast;
        _addrlen = addrlen;
        memcpy(&_addr,addr,_addrlen);
    }
    void on_read(OnReadFunc func) {
        _on_read = func;
    }
    void cleanup() override {
        if (_fd != -1) close(_fd);
    }
    int epollIN() override {
        while(true) {
            int sz;
            errno_c ret = err_chk(ioctl(_fd, FIONREAD, &sz),"udp ioctl");
            if (ret) {
                on_error(ret, "Query datagram size error");
            } else {
                if (sz==0) { 
                    break;
                }
                void* buffer = alloca(sz);
                _send_addrlen = sizeof(_send_addr);
                ssize_t n = recvfrom(_fd, buffer, sz, 0, (sockaddr*)&_send_addr, &_send_addrlen);
                if (n<0) {
                    errno_c ret;
                    if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                        on_error(ret, "udp recvfrom");
                    }
                    _send_addrlen = 0;
                } else {
                    if (n != sz) {
                        log::warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    log::debug()<<"on_read"<<std::endl;
                    if (_on_read) _on_read(buffer, n);
                }
            }
        }
        return HANDLED;
    }
    int epollOUT() override {
        is_writeable = true;
        return HANDLED;
    }
protected:

    int send(const void* buf, int len, sockaddr* addr, socklen_t alen) {
        if (!is_writeable) {
            return 0;
        }
        int ret = sendto(_fd, buf, len, 0, (sockaddr*)addr, alen);
        if (ret==-1) {
            errno_c err;
            on_error(err, "UDP send datagram");
            is_writeable=false;
        } else if (ret != len) {
            log::error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }
        return ret;
    }

    socklen_t _addrlen = 0;
    sockaddr_storage _addr;
    socklen_t _send_addrlen = 0;
    sockaddr_storage _send_addr;
    int _fd = -1;
    bool is_writeable = true;
    OnReadFunc _on_read;
    bool _broadcast = false;
};

class UdpClientBase : public UdpBase {
public:
    UdpClientBase():UdpBase("udp client") {}
    virtual error_c start_with(IOLoop* loop) override {
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp client socket");
        }
        if (_broadcast) {
            int yes = 1;
            errno_c ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_BROADCAST, (void *) &yes, sizeof(yes)),"setsockopt(broadcast)");
            if (ret) return ret;
        }
        is_writeable = true;
        return loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
    }
    int write(const void* buf, int len) override {
        return send(buf, len, (sockaddr*)&_addr, _addrlen);
    }
};

class UdpServerBase : public UdpBase {
public:
    UdpServerBase():UdpBase("udp server") {}
    virtual error_c start_with(IOLoop* loop) override {
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp server socket");
        }
        if (_broadcast) {
            int yes = 1;
            errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
            if (ret) {
                close(_fd);
                return ret;
            }
        }
        errno_c ret = err_chk(bind(_fd, (sockaddr *)&_addr, _addrlen), "udp server bind");
        if (ret) return ret;
        return loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
    }

    int write(const void* buf, int len) override {
        log::debug()<<"udp write"<<std::endl;
        if (_send_addrlen == 0) {
            return 0;
        }
        log::debug()<<"send"<<std::endl;
        return send(buf,len,(sockaddr*)&_send_addr, _send_addrlen);
    }

    bool _broadcast = false;
};

class UdpClientImpl : public UdpClient {
public:
    UdpClientImpl(const std::string& name):_name(name) {}

    void init(const std::string& host, int port, IOLoop* loop) override {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver.on_error(on_err);
        _addr_resolver.on_resolve([this](addrinfo* ai) {
            _udp.init(ai->ai_addrlen, ai->ai_addr);
            error_c ret = _loop->execute(&_udp);
            if (ret) { on_error(ret,_name);
            } else {
                _is_writeable = true;
                log::debug()<<"on connect client ok"<<std::endl;
                if (_on_connect) { _on_connect();
                }
            }
        });
        _addr_resolver.init_resolving_client(host,port,loop);
        _loop = loop;
    }

    void init_broadcast(int port, IOLoop* loop, const std::string& interface) override {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _loop = loop;
        _addr_resolver.on_error(on_err);
        _addr_resolver.on_resolve([this](addrinfo* ai) {
            _udp.init(ai->ai_addrlen, ai->ai_addr,true);
            error_c ret = _loop->execute(&_udp);
            if (ret) { on_error(ret,_name);
            } else {
                _is_writeable = true;
                log::debug()<<"on connect broadcast ok"<<std::endl;
                if (_on_connect) { _on_connect();
                }
            }
        });
        _addr_resolver.init_resolving_broadcast(port,loop,interface);
    }

    void on_read(OnReadFunc func) {
        _udp.on_read(func);
    }
    void on_connect(OnEventFunc func) {
        _on_connect = func;
    }
    int write(const void* buf, int len) { 
        if (!_is_writeable) {
            return 0;
        }
        return _udp.write(buf,len);
    }

    std::string _name;
    IOLoop *_loop;
    UdpClientBase _udp;
    AddressResolver _addr_resolver;
    bool _is_writeable = false;
    OnEventFunc _on_connect;
};

std::unique_ptr<UdpClient> UdpClient::create(const std::string& name) {
    return std::unique_ptr<UdpClient>{new UdpClientImpl(name)};
}

class UdpServerImpl : public UdpServer {
public:
    UdpServerImpl(const std::string& name):_name(name) {};
    void init(int port, IOLoop* loop, const std::string& host_or_interface="", bool broadcast=false) override {
        _loop = loop;
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver.on_error(on_err);
        _addr_resolver.on_resolve([this,broadcast](addrinfo* ai) {
            _udp._broadcast = broadcast;
            _udp.init(ai->ai_addrlen, ai->ai_addr);
            error_c ret = _loop->execute(&_udp);
            if (ret) { on_error(ret,"udp server");
            } else {
                _is_writeable = true;
            }
        });
        if (broadcast) {
            _addr_resolver.init_resolving_broadcast(port,_loop,host_or_interface);
        } else {
            _addr_resolver.init_resolving_server(port,_loop,host_or_interface);
        }
    }
    int write(const void* buf, int len) override {
        log::debug()<<"svr write"<<std::endl;
        if (!_is_writeable) return 0;
        return _udp.write(buf,len);
    }
    void on_read(OnReadFunc func) override {
        _udp.on_read(func);
    };

    std::string _name;
    IOLoop *_loop;
    AddressResolver _addr_resolver;
    UdpServerBase _udp;
    bool _is_writeable = false;
};

std::unique_ptr<UdpServer> UdpServer::create(const std::string& name) {
    return std::unique_ptr<UdpServer>{new UdpServerImpl(name)};
}