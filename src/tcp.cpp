#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include "err.h"
#include "log.h"
#include "epoll.h"
#include "tcp.h"

#include "addrinfo.h"

class TcpClientBase : public IOPollable, public IOWriteable {
public:
    void init(socklen_t addrlen, sockaddr *addr) {
        _addrlen = addrlen;
        memcpy(&_addr,addr,_addrlen);
    }
    void on_read_func(OnReadFunc func) {
        _on_read = func;
    }
    void on_connect_func(OnEventFunc func) {
        _on_connect = func;
    }
    void on_close_func(OnEventFunc func) {
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

    int write(const void* buf, int len) {
        if (!is_writeable) {
            log::debug()<<"write not writable"<<std::endl;
            return 0;
        }
        int ret = send(_fd, buf, len, 0);
        if (ret==-1) {
            errno_c err;
            on_error(err, "UDP send datagram");
            is_writeable=false;
        } else if (ret != len) {
            log::error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }
        return ret;
    }

    errno_c check() {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }

    void events(IOLoop* loop, uint32_t evs) override {
        log::debug()<<"tcp event "<<evs<<std::endl;
        if (evs & EPOLLIN) {
            evs &= ~EPOLLIN;
            log::debug()<<"EPOLLIN"<<std::endl;
            errno_c ret = check();
            if (ret) {    on_error(ret);
            } else while(true) {
                int sz;
                ret = err_chk(ioctl(_fd, FIONREAD, &sz),"udp ioctl");
                if (ret) {
                    on_error(ret, "Query datagram size error");
                } else {
                    if (sz==0) {
                        log::debug()<<"nothing to read"<<std::endl;
                        break;
                    }
                    void* buffer = alloca(sz);
                    ssize_t n = recv(_fd, buffer, sz, 0);
                    if (n<0) {
                        errno_c ret;
                        if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                            on_error(ret, "udp recvfrom");
                        }
                    } else {
                        if (n != sz) {
                            log::warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                        }
                        log::debug()<<"on_read"<<std::endl;
                        if (_on_read) _on_read(buffer, n);
                    }
                }
            }
        }
        if (evs & EPOLLOUT) {
            log::debug()<<"EPOLLOUT"<<std::endl;
            evs &= ~EPOLLOUT;
            is_writeable = true;
            if (!is_connected) {
                if (_on_connect) _on_connect();
                is_connected=true;
            }
            
        }
        if (evs & EPOLLRDHUP) {
            log::debug()<<"EPOLLRDHUP"<<std::endl;
            evs &= ~EPOLLRDHUP;
            //errno_c ret = err_chk(shutdown(_fd,SHUT_WR),"shutdown");
            //if (ret) { on_error(ret,"tcp event");
            //}
            error_c ret = loop->del(_fd, this);
            if (ret) { on_error(ret,"tcp event");
            }
            if (_on_close) _on_close();
            cleanup();
        }
        if (evs & EPOLLHUP) {
            evs &= ~EPOLLHUP;
            log::debug()<<"EPOLLHUP"<<std::endl;
            error_c ret = loop->del(_fd, this);
            if (ret) { on_error(ret,"tcp event");
            }
            if (_on_close) _on_close();
            cleanup();
        }
        if (evs & EPOLLERR) {
            log::debug()<<"EPOLLERR"<<std::endl;
            evs &= ~EPOLLERR;
            errno_c ret = check();
            on_error(ret,"sock error");
        }
        if (evs) {
            log::warning()<<"TCP unexpected event "<<evs<<std::endl;
        }
    }
    error_c start_with(IOLoop* loop) override {
        _fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("tcp client socket");
        }
        int yes = 1;
        //TODO: SO_PRIORITY SO_RCVBUF SO_SNDBUF SO_RCVLOWAT SO_SNDLOWAT SO_RCVTIMEO SO_SNDTIMEO SO_TIMESTAMP SO_TIMESTAMPNS SO_INCOMING_CPU
        errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
        if (ret) {
            close(_fd);
            return ret;
        }
        ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
        if (ret) {
            close(_fd);
            return ret;
        }
        ret = err_chk(connect(_fd,(sockaddr*)&_addr, _addrlen),"connect");
        if (ret && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            return ret;
        }
        return loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, this);
    }
private:
    socklen_t _addrlen = 0;
    sockaddr_storage _addr;
    int _fd = -1;
    bool is_writeable = false;
    bool is_connected = false;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    OnEventFunc _on_connect;
};


class TcpClient::TcpClientImpl : public error_handler {
public:
    TcpClientImpl(const std::string& name):_name(name) {}
    void init(const std::string& host, int port, IOLoop* loop) {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _tcp.on_error_func(on_err);
        _addr_resolver.on_error_func(on_err);
        _addr_resolver.on_resolve_func([this](addrinfo* ai) {
            _tcp.init(ai->ai_addrlen, ai->ai_addr);
            error_c ret = _loop->execute(&_tcp);
            if (ret) { on_error(ret,_name);
            }
        });
        _addr_resolver.init_resolving_client(host,port,loop);
        _loop = loop;
    }
    std::string _name;
    IOLoop *_loop;
    TcpClientBase _tcp;
    AddressResolver _addr_resolver;
};

TcpClient::TcpClient(const std::string& name):_impl{new TcpClientImpl{name}} {
    _impl->on_error_func([this](error_c& ec){ on_error(ec,"tcp client");});
}
TcpClient::~TcpClient() {}
void TcpClient::on_read_func(OnReadFunc func) {
    _impl->_tcp.on_read_func(func);
}
void TcpClient::on_connect_func(OnEventFunc func) {
    _impl->_tcp.on_connect_func(func);
}
void TcpClient::on_close_func(OnEventFunc func) {
    _impl->_tcp.on_close_func(func);
}
void TcpClient::init(const std::string& host, int port, IOLoop* loop) {
    _impl->init(host,port,loop);
}
int TcpClient::write(const void* buf, int len) {
    return _impl->_tcp.write(buf,len);
}
