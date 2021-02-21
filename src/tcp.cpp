#include <unistd.h>
#include <cstring>
#include <utility>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "err.h"
#include "log.h"
#include "loop.h"
#include "tcp.h"

#include "addrinfo.h"
static Log::Log log("tcp");
class TcpClientBase : public IOPollable, public IOWriteable, public error_handler {
public:
    TcpClientBase():IOPollable("tcp client") {}
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
        int ret = send(_fd, buf, len, MSG_NOSIGNAL);
        if (ret==-1) {
            errno_c err;
            on_error(err, "TCP send datagram");
            is_writeable=false;
        } else if (ret != len) {
            log.error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }
        return ret;
    }

    auto check() -> errno_c {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }

    auto epollIN() -> int override {
        errno_c ret = check();
        if (ret) {    
            on_error(ret);
        } else while(true) {
            int sz;
            ret = err_chk(ioctl(_fd, FIONREAD, &sz),"tcp ioctl");
            if (ret) {
                on_error(ret, "Query datagram size error");
            } else {
                if (sz==0) {
                    log.debug()<<"nothing to read"<<std::endl;
                    break;
                }
                void* buffer = alloca(sz);
                ssize_t n = recv(_fd, buffer, sz, 0);
                if (n<0) {
                    errno_c ret;
                    if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                        on_error(ret, "tcp recv");
                    }
                } else {
                    if (n != sz) {
                        log.warning()<<"Data declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    log.debug()<<"on_read"<<std::endl;
                    if (_on_read) _on_read(buffer, n);
                }
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
        errno_c ret = check();
        on_error(ret,"sock error");
        return HANDLED;
    }

    auto start_with(IOLoop* loop) -> error_c override {
        _fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("tcp client socket");
        }
        int yes = 1;
        //TODO: SO_PRIORITY SO_RCVBUF SO_SNDBUF SO_RCVLOWAT SO_SNDLOWAT SO_RCVTIMEO SO_SNDTIMEO SO_TIMESTAMP SO_TIMESTAMPNS SO_INCOMING_CPU
        /*errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
        if (ret) {
            close(_fd);
            return ret;
        }*/
        error_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
        if (ret) {
            close(_fd);
            return ret;
        }
        ret = _addr.connect(_fd);
        if (ret && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            return ret;
        }
        _loop = loop;
        return loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, this);
    }
private:
    //socklen_t _addrlen = 0;
    //sockaddr_storage _addr;
    SockAddr _addr;
    int _fd = -1;
    bool is_writeable = false;
    bool is_connected = false;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    OnEventFunc _on_connect;
    IOLoop* _loop;
};


class TcpClientImpl : public TcpClient {
public:
    TcpClientImpl(std::string name):_name(std::move(name)),_addr_resolver{AddressResolver::create()} {
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
    void init(const std::string& host, uint16_t port, IOLoop* loop) override {
        _loop = loop;
        _addr_resolver->remote(host,port,loop);
    }

    void init_service(const std::string& service_name, IOLoop* loop, const std::string& interface="") override {
        _loop = loop;
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

auto TcpClient::create(const std::string& name) -> std::unique_ptr<TcpClient> {
    return std::unique_ptr<TcpClient>{new TcpClientImpl(name)};
}

class TcpSocketImpl final : public IOPollable, public TcpSocket {
public:
    TcpSocketImpl(int fd):IOPollable("tcp socket"),_fd(fd) {}
    ~TcpSocketImpl() final { cleanup();
    }
    void on_read(OnReadFunc func) override {
        _on_read = func;
    }
    void on_close(OnEventFunc func) override {
        _on_close = func;
    }
    auto check() -> errno_c {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }
    auto epollIN() -> int override {
        while(true) {
            int sz;
            errno_c ret = err_chk(ioctl(_fd, FIONREAD, &sz),"tcp ioctl");
            if (ret) {
                on_error(ret, "Query data size error");
            } else {
                if (sz==0) { 
                    error_c ret = _loop->del(_fd, this);
                    on_error(ret,"tcp socket cleanup");
                    cleanup();
                    if (_on_close) _on_close();
                    return STOP;
                }
                void* buffer = alloca(sz);
                int n = recv(_fd, buffer, sz, 0);
                if (n == -1) {
                    errno_c ret;
                    if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                        on_error(ret, "tcp recv");
                    }
                } else {
                    if (n != sz) {
                        log.warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    log.debug()<<"on_read"<<std::endl;
                    if (_on_read) _on_read(buffer, n);
                }
            }
        }
        return HANDLED;
    }
    auto epollOUT() -> int override {
        _is_writeable = true;
        write_allowed();
        return HANDLED;
    }
    auto epollERR() -> int override {
        return HANDLED;
    }
    auto epollHUP() -> int override {
        error_c ret = _loop->del(_fd, this);
        if (ret) on_error(ret, "loop del");
        cleanup();
        if (_on_close) { _on_close();
        }
        return HANDLED;
    }
    auto epollEvent(int events) -> bool override {
        if (events & (EPOLLIN | EPOLLERR)) {
            errno_c err = check();
            if (err || (events & EPOLLERR)) on_error(err,"tcp socket error");
        }
        return false;
    }
    auto start_with(IOLoop* loop) -> error_c override {
        _loop = loop;
        return loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
    }
    void cleanup() override {
        if (_fd != -1) {
            close(_fd);
            _fd = -1;
        }
    }
    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) return 0;
        ssize_t n = send(_fd, buf, len, MSG_NOSIGNAL);
        _is_writeable = n==len;
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                if (ret==std::error_condition(std::errc::broken_pipe)) {
                    _loop->del(_fd,this);
                    cleanup();
                    if (_on_close) _on_close();
                }
                on_error(ret, "tcp send");
            }
        }
        return n;
    }
private:
    int _fd = -1;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    IOLoop* _loop;
    bool _is_writeable = true;
};


class TcpServerBase : public IOPollable, public error_handler {
public:
    TcpServerBase():IOPollable("tcp server") {}
    void init(addrinfo* ai) { 
        _addr.init(ai); 
    }
        
    void on_connect(TcpServer::OnConnectFunc func) {
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
    auto check() -> errno_c {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }
    auto epollIN() -> int override {
        errno_c ret = check();
        if (ret) { on_error(ret);
        } else while(true) {
            sockaddr_storage client_addr;
            socklen_t ca_len = sizeof(client_addr);
            //log.debug()<<"before accept"<<std::endl;
            int client = accept(_fd, (sockaddr *) &client_addr, &ca_len);
            //log.debug()<<"after accept "<<client<<std::endl;
            if (client==-1) {
                errno_c ret("accept");
                if (ret!=std::error_condition(std::errc::resource_unavailable_try_again) &&
                    ret!=std::error_condition(std::errc::operation_would_block)) {
                        on_error(ret);
                }
                break;
            } else {
                int yes = 1;
                errno_c ret = err_chk(setsockopt(client,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
                if (ret) {
                    on_error(ret);
                    close(client);
                    break;
                }
                int flags = fcntl(client, F_GETFL);
                if (flags == -1) {
                    errno_c ret("fcntl getfl");
                    on_error(ret);
                    close(client);
                    break;
                }
                ret = fcntl(client, F_SETFL, flags | O_NONBLOCK);
                if (ret) {
                    on_error(ret,"fcntl setfl");
                    close(client);
                    break;
                }
                auto sock = new TcpSocketImpl(client);
                sock->start_with(_loop);
                std::unique_ptr<TcpSocket> socket = std::unique_ptr<TcpSocket>{sock};
                on_connect(socket, (struct sockaddr *) &client_addr, ca_len);
            }
        }
        return HANDLED;
    }
    auto epollERR() -> int override {
        errno_c ret = check();
        on_error(ret,"sock error");
        return HANDLED;
    }

    auto socket_create() -> error_c {
        if (_fd!=-1) {
            _loop->del(_fd, this);
            close(_fd);
        }
        _fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("tcp server socket");
        }
        int yes = 1;
        //TODO: SO_PRIORITY SO_RCVBUF SO_SNDBUF SO_RCVLOWAT SO_SNDLOWAT SO_RCVTIMEO SO_SNDTIMEO SO_TIMESTAMP SO_TIMESTAMPNS SO_INCOMING_CPU
        /*errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
        if (ret) {
            close(_fd);
            return ret;
        }*/
        error_c ret = _addr.bind(_fd);
        if (ret) { // && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            _fd = -1;
            return ret;
        }
        ret = err_chk(listen(_fd,5),"listen");
        if (ret) { // && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            _fd = -1;
            return ret;
        }
        return _loop->add(_fd, EPOLLIN, this);
    }

    void create_service(AvahiGroup* g) {
        error_c ret = socket_create();
        if (ret) {
            log.error()<<ret<<std::endl;
            return;
        }
        SockAddr port_finder(_fd);
        error_c ec = g->add_service(
            CAvahiService(_service_name,"_pktstream._tcp").set_ipv4().set_interface(_service_itf),
            port_finder.port()
        );
        std::string name = std::to_string(port_finder.port())+"_"+_service_itf+"_"+g->host_name()+".tcp";
        ec = g->add_service(
            CAvahiService(name,"_portclaim._tcp").set_ipv4().set_interface(_service_itf),
            port_finder.port()
        );
        if (ec) {
            log.error()<<"Error adding service "<<_service_name<<": "<<ec<<std::endl;
            ec = g->reset();
            if (ec) log.error()<<"Error reset service "<<_service_name<<": "<<ec<<std::endl;
        } else {
            ec = g->commit();
            if (ec) log.error()<<"Error commit service "<<_service_name<<": "<<ec<<std::endl;
        }
    }

    auto start_with(IOLoop* loop) -> error_c override {
        _loop = loop;
        
        if (_service_name.empty()) { return socket_create();
        }
        _group = _loop->get_register_group();
        _group->on_create([this](AvahiGroup* g){ create_service(g); 
        });
        _group->on_collision([this](AvahiGroup* g){ 
            log.info()<<"Group collision"<<std::endl;
            g->reset();
            create_service(g);
        });
        _group->on_established([this](AvahiGroup* g){ 
            log.info()<<"Service registered"<<std::endl;
            
        });
        _group->on_failure([](error_c ec){ 
            log.info()<<"Group error"<<ec<<std::endl;
        });
        _group->create();
        return error_c();
    }

    void on_connect(std::unique_ptr<TcpSocket>& socket, sockaddr* addr, socklen_t len) {
        if (_on_connect) {
            _on_connect(socket, addr, len);
        } else {
            log.warning()<<"No socket handler"<<std::endl;
        }
    }

    void set_service_info(const std::string& name, const std::string& itf) {
        _service_name = name;
        _service_itf = itf;
    }
    void clear_service_info() {
        _service_itf.clear();
        _service_name.clear();
    }
private:
    SockAddr _addr;
    std::string _service_name = "";
    std::string _service_itf = "";
    std::unique_ptr<AvahiGroup> _group;
    int _fd = -1;
    bool is_writeable = false;
    bool is_connected = false;
    OnEventFunc _on_close;
    TcpServer::OnConnectFunc _on_connect;
    IOLoop* _loop = nullptr;
};

class TcpServerImpl : public TcpServer {
public:
    TcpServerImpl(std::string  name):_name(std::move(name)),_addr_resolver{AddressResolver::create()} {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _tcp.on_error(on_err);
        _addr_resolver->on_error(on_err);
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _tcp.init(ai);
            error_c ret = _tcp.start_with(_loop);
            if (ret) { on_error(ret,"tcp client");
            }
        });
    };
    
    void init(uint16_t port, IOLoop* loop, const std::string& host="") override {
        _loop = loop;
        _tcp.clear_service_info();
        _addr_resolver->local(port,_loop,host);
    }
    void init_interface(const std::string& interface, uint16_t port, IOLoop* loop) override {
        _loop = loop;
        _tcp.clear_service_info();
        _addr_resolver->local_interface(interface,port,_loop);
    }
    void init_service(const std::string& service_name, const std::string& interface, IOLoop* loop) override {
        _loop = loop;
        _tcp.set_service_info(service_name,interface);
        if (interface.empty()) { 
            _addr_resolver->local(0,_loop);
            return;
        }
        _addr_resolver->local_interface(interface,0,_loop);
    }
    void on_connect(OnConnectFunc func) override {
        _tcp.on_connect(func);
    }
    void on_close(OnEventFunc func) override {
        _tcp.on_close(func);
    }
private:
    std::string _name;
    IOLoop *_loop;
    std::unique_ptr<AddressResolver> _addr_resolver;
    TcpServerBase _tcp;
};

auto TcpServer::create(const std::string& name) -> std::unique_ptr<TcpServer> {
    return std::unique_ptr<TcpServer>{new TcpServerImpl(name)};
}
