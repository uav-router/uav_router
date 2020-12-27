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

struct FD {
    int* _fd;
    FD(int& fd): _fd(&fd) {}
    ~FD() {
        if (_fd && *_fd!=-1) close(*_fd);
    }
    void clear() {_fd=nullptr;}
};
class UdpBase : public IOPollable, public IOWriteable, public error_handler {
public:
    UdpBase(const std::string n):IOPollable(n) {}
    void init(socklen_t addrlen, sockaddr *addr, bool broadcast=false) {
        _broadcast=broadcast;
        _multicast=false;
        _addrlen = addrlen;
        memcpy(&_addr,addr,_addrlen);
    }
    void init_multicast(socklen_t addrlen, sockaddr *addr, socklen_t itflen, sockaddr *itf, unsigned char ttl) {
        _addrlen = addrlen;
        memcpy(&_addr,addr,_addrlen);
        _itflen = itflen;
        memcpy(&_itf,itf,_itflen);
        _ttl = ttl;
        _broadcast=false;
        _multicast=true;
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
    socklen_t _itflen = 0;
    sockaddr_storage _itf;
    unsigned char _ttl = 0;
    int _fd = -1;
    bool is_writeable = true;
    OnReadFunc _on_read;
    bool _broadcast = false;
    bool _multicast = false;
};

class UdpClientBase : public UdpBase {
public:
    UdpClientBase():UdpBase("udp client") {}
    virtual error_c start_with(IOLoop* loop) override {
        FD watcher(_fd);
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp client socket");
        }
        if (_broadcast) {
            int yes = 1;
            errno_c ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_BROADCAST, (void *) &yes, sizeof(yes)),"setsockopt(broadcast)");
            if (ret) return ret;
        } else if (_multicast) { // multicast socket
            if (_ttl) {
                errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&_ttl, sizeof(_ttl)),"multicast ttl");
                if (ret) return ret;
            }
            if (_itflen && _itf.ss_family==AF_INET && ((sockaddr_in*)&_itf)->sin_addr.s_addr!=htonl(INADDR_ANY)) {
                auto itf = ((sockaddr_in*)&_itf)->sin_addr.s_addr;
                errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_IF, (void *)&itf, sizeof(itf)),"multicast itf");
                if (ret) return ret;
            }
        }
        is_writeable = true;
        auto ret = loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        if (ret) return ret;
        watcher.clear();
        return error_c();
    }
    int write(const void* buf, int len) override {
        return send(buf, len, (sockaddr*)&_addr, _addrlen);
    }
};

class UdpServerBase : public UdpBase {
public:
    UdpServerBase():UdpBase("udp server") {}
    virtual error_c start_with(IOLoop* loop) override {
        FD watcher(_fd);
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp server socket");
        }
        if (_broadcast) {
            int yes = 1;
            errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
            if (ret) return ret;
        }
        if (_multicast) {
            int yes = 1;
            error_c ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)),"reuse port");
            if (ret) {
                log::warning()<<"Multicast settings: "<<ret.place()<<": "<<ret.message()<<std::endl;
                ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),"multicast reuseaddr");
                if (ret) return ret;
            }
            sockaddr_in any;
            memset(&any, 0, sizeof(any));
            any.sin_family = AF_INET;
            any.sin_addr.s_addr = htonl(INADDR_ANY);
            any.sin_port = ((sockaddr_in*)&_addr)->sin_port;
            ret = err_chk(bind(_fd, (sockaddr *)&any, sizeof(any)), "udp server multicast bind");
            if (ret) return ret;
            ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = ((sockaddr_in*)&_addr)->sin_addr.s_addr;
            mreq.imr_interface.s_addr = ((sockaddr_in*)&_itf)->sin_addr.s_addr;
            ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)),"add membership");
            if (ret) return ret;
        } else {
            error_c ret = err_chk(bind(_fd, (sockaddr *)&_addr, _addrlen), "udp server bind");
            if (ret) return ret;
        }
        error_c ret = loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        if (ret) return ret;
        watcher.clear();
        return error_c();
    }

    void cleanup() override {
        if (_fd != -1) {
            if (_multicast) {
                ip_mreq mreq;
                mreq.imr_multiaddr.s_addr = ((sockaddr_in*)&_addr)->sin_addr.s_addr;
                mreq.imr_interface.s_addr = ((sockaddr_in*)&_itf)->sin_addr.s_addr;
                error_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)),"drop membership");
                on_error(ret);
            }
            close(_fd);
            _fd = -1;
        }
    }

    int write(const void* buf, int len) override {
        log::debug()<<"udp write"<<std::endl;
        if (_send_addrlen == 0) {
            return 0;
        }
        log::debug()<<"send"<<std::endl;
        return send(buf,len,(sockaddr*)&_send_addr, _send_addrlen);
    }
};

class UdpClientImpl : public UdpClient {
public:
    UdpClientImpl(const std::string& name):_name(name),_addr_resolver{AddressResolver::create()} {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver->on_error(on_err);
    }
    void execute() {
        error_c ec = _loop->execute(&_udp);
        if (!on_error(ec,_name)) {
            _is_writeable = true;
            if (_on_connect) { _on_connect();
            }
        }
    }
    void init(const std::string& host, uint16_t port, IOLoop* loop) override {
        _loop = loop;
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai->ai_addrlen, ai->ai_addr);
            execute();
        });
        _addr_resolver->init_resolving_client(host,port,loop);
        
    }

    void init_broadcast(uint16_t port, IOLoop* loop, const std::string& interface) override {
        _loop = loop;
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai->ai_addrlen, ai->ai_addr,true);
            execute();
        });
        _addr_resolver->init_resolving_broadcast(port,loop,interface);
    }

    void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface, int ttl) override {
        _loop = loop;
        log::debug()<<"init multicast started"<<std::endl;
        sockaddr_storage addr;
        socklen_t addr_len;
        _addr_resolver->on_resolve([this,&addr,&addr_len](addrinfo* addr_info) {
            memcpy(&addr,addr_info->ai_addr,addr_info->ai_addrlen);
            addr_len = addr_info->ai_addrlen;
        });
        error_c ret = _addr_resolver->resolve_ip4(address,port);
        if (ret) {
            on_error(ret,"resolve multicast address");
            return;
        }
        sockaddr_storage maddr;
        socklen_t maddr_len = addr_len;
        memcpy(&maddr,&addr,addr_len);

        ret = _addr_resolver->resolve_interface_ip4(interface,port,AddressResolver::Interface::ADDRESS);
        if (ret) {
            sockaddr_in* a = (sockaddr_in*)&addr;
            addr_len = sizeof(sockaddr_in);
            a->sin_family = AF_INET;
            a->sin_addr.s_addr = htonl(INADDR_ANY);
            a->sin_port = port;
        }
        _udp.init_multicast(maddr_len, (sockaddr*)&maddr, addr_len, (sockaddr*)&addr, ttl);
        execute();
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
    std::unique_ptr<AddressResolver> _addr_resolver;
    bool _is_writeable = false;
    OnEventFunc _on_connect;
};

std::unique_ptr<UdpClient> UdpClient::create(const std::string& name) {
    return std::unique_ptr<UdpClient>{new UdpClientImpl(name)};
}

class UdpServerImpl : public UdpServer {
public:
    UdpServerImpl(const std::string& name):_name(name),_addr_resolver{AddressResolver::create()} {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver->on_error(on_err);
    };
    void init(uint16_t port, IOLoop* loop, const std::string& host_or_interface="", bool broadcast=false) override {
        _loop = loop;
        _addr_resolver->on_resolve([this,broadcast](addrinfo* ai) {
            _udp.init(ai->ai_addrlen, ai->ai_addr, broadcast);
            error_c ec = _loop->execute(&_udp);
            _is_writeable = !on_error(ec,"udp server");
        });
        if (broadcast) {
            _addr_resolver->init_resolving_broadcast(port,_loop,host_or_interface);
        } else {
            _addr_resolver->init_resolving_server(port,_loop,host_or_interface);
        }
    }
    void init_multicast(const std::string& address, uint16_t port, IOLoop* loop, const std::string& interface="") {
        _loop = loop;
        log::debug()<<"init multicast started"<<std::endl;
        sockaddr_storage addr;
        socklen_t addr_len;
        _addr_resolver->on_resolve([this,&addr,&addr_len](addrinfo* addr_info) {
            memcpy(&addr,addr_info->ai_addr,addr_info->ai_addrlen);
            addr_len = addr_info->ai_addrlen;
        });
        error_c ret = _addr_resolver->resolve_ip4(address,port);
        if (ret) {
            on_error(ret,"resolve multicast address");
            return;
        }
        sockaddr_storage maddr;
        socklen_t maddr_len = addr_len;
        memcpy(&maddr,&addr,addr_len);

        ret = _addr_resolver->resolve_interface_ip4(interface,port,AddressResolver::Interface::ADDRESS);
        if (ret) {
            sockaddr_in* a = (sockaddr_in*)&addr;
            addr_len = sizeof(sockaddr_in);
            a->sin_family = AF_INET;
            a->sin_addr.s_addr = htonl(INADDR_ANY);
            a->sin_port = port;
        }
        _udp.init_multicast(maddr_len, (sockaddr*)&maddr, addr_len, (sockaddr*)&addr,0);
        error_c ec = _loop->execute(&_udp);
        _is_writeable = !on_error(ec,"udp server");
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
    std::unique_ptr<AddressResolver> _addr_resolver;
    UdpServerBase _udp;
    bool _is_writeable = false;
};

std::unique_ptr<UdpServer> UdpServer::create(const std::string& name) {
    return std::unique_ptr<UdpServer>{new UdpServerImpl(name)};
}