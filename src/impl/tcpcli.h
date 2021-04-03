#ifndef __TCPCLI__H__
#define __TCPCLI__H__
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <chrono>
using namespace std::chrono_literals;

#include "../err.h"
#include "../loop.h"
#include "../log.h"


class TcpClientImpl;

class TCPClientStream: public Client {
public:
    TCPClientStream(const std::string& name, int fd):_name(name), _fd(fd) {}
    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) return 0;
        ssize_t n = send(_fd, buf, len, MSG_NOSIGNAL);
        _is_writeable = n==len;
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                on_error(ret, "uart write");
            }
        }
        return n;
    }

    auto get_peer_name() -> const std::string& override {
        return _name;
    }

    const std::string& _name;
    int _fd;

    friend class TcpClientImpl;
};

class TcpClientImpl : public TcpClient, public IOPollable {
public:
    TcpClientImpl(const std::string name, IOLoopSvc* loop):IOPollable(name),_loop(loop),_resolv(loop->address()),_timer(loop->timer()) {
        _timer->shoot([this](){ connect(); });
        _addr = _addresses.end();
    }

    ~TcpClientImpl() override {
        _exists = false;
        cleanup();
    }

    auto init(const std::string& host, uint16_t port) -> error_c override {
        return _resolv->init(host, port, [this](SockAddrList&& a) {
            _addresses = std::move(a);
            _addr = _addresses.begin();
            connect();
        });
    }

    void connect() {
        if (_addresses.empty()) {
            _resolv->requery();
            return;
        }   
        if (_addr==_addresses.end()) {
            _timer->arm_oneshoot(3s);
            _addr = _addresses.begin();
            return;
        }
        error_c ret = create_connection();
        if (ret) {
            on_error(ret,"create tcp connection");
            _addr++;
            connect();
        }
    }

    auto connect_to_peer() -> error_c {
        error_c ret = _addr->connect(_fd);
        if (ret && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            _fd=-1;
            return ret;
        }
        ret = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, this);
        if (ret) {
            close(_fd);
            _fd=-1;
        }
        return ret;
    }

    void connect_to_peer_retry() {
        error_c ret = connect_to_peer();
        if (ret) {
            on_error(ret, "connect to peer");
            _addr++;
            connect();
        }
    }

    auto create_connection() -> error_c {
        if (_fd!=-1) { 
            cleanup();
            if (!_exists) return error_c();
        }
        _fd = socket(_addr->family(), SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("tcp client socket");
        }
        int yes = 1;
        //TODO: SO_PRIORITY SO_RCVBUF SO_SNDBUF SO_RCVLOWAT SO_SNDLOWAT SO_RCVTIMEO SO_SNDTIMEO SO_TIMESTAMP SO_TIMESTAMPNS SO_INCOMING_CPU
        //errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
        //if (ret) {
        //    close(_fd);
        //    return ret;
        //}
        error_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
        if (ret) { return ret;
        }
        auto zeroconf = _loop->zeroconf();
        if (!zeroconf) {
            peer_name = _addr->format(SockAddr::REG_SERVICE);
            return connect_to_peer();
        }
        peer_name = _loop->zeroconf()->query_service_name(*_addr, SOCK_STREAM);
        if (peer_name.empty()) {
            peer_name = _addr->format(SockAddr::REG_SERVICE);
            return connect_to_peer();
        }
        // subscribe to peer_name

        auto any = SockAddr::any(_addr->family());
        ret = any.bind(_fd);
        if (ret) { return ret;
        }
        SockAddr myaddr(_fd);
        _group = _loop->zeroconf()->get_register_group();
        _group->on_create([this, port = myaddr.port()](AvahiGroup* g){ 
            error_c ec = g->add_service(
                CAvahiService(name,"_pktstreamnames._tcp"),
                port
            );
            if (ec) {
                on_error(ec,"add service");
                ec = g->reset();
                on_error(ec,"reset service");
                connect_to_peer_retry();
            } else {
                ec = g->commit();
                on_error(ec,"commit service");
            }
        });
        _group->on_collision([this](AvahiGroup* g){ 
            log.error()<<"Collision on endpoint name "<<name<<std::endl;
            g->reset();
            connect_to_peer_retry();
        });
        _group->on_established([this](AvahiGroup* g){ 
            log.info()<<"Service "<<name<<" registered"<<std::endl;
            connect_to_peer_retry();
        });
        _group->on_failure([this](error_c ec){ 
            on_error(ec,"registration failure");
            connect_to_peer_retry();
        });
        _group->create();
        return error_c();
    }

    void cleanup() override {
        if (_fd != -1) {
            on_error(err_chk(close(_fd),"close"));
            _fd = -1;
        }
    }

    auto error() -> bool {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        on_error(ret);
        if (ret) return true;
        return false;
    }

    auto epollIN() -> int override {
        if (error()) return HANDLED;
        while(true) {
            int sz;
            error_c ret = err_chk(ioctl(_fd, FIONREAD, &sz),"tcp ioctl");
            if (ret) {
                on_error(ret, "Query buffer size error");
                if (!_exists) return STOP;
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
                        if (!_exists) return STOP;
                    }
                } else {
                    if (n != sz) {
                        log.warning()<<"Data declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    if (auto client = cli()) client->on_read(buffer, n);
                    if (!_exists) return STOP;
                }
            }
        }
        return HANDLED;
    }

    auto epollOUT() -> int override {
        auto client = cli();
        return HANDLED;
    }

    auto epollRDHUP() -> int override {
        if (_fd==-1) {
            error_c ret = _loop->poll()->del(_fd, this);
            if (ret) { 
                on_error(ret,"tcp event");
                if (!_exists) return STOP;
            }
        }
        cleanup();
        //if client exists - reconnect
        return HANDLED;
    }

    auto epollHUP() -> int override { return epollRDHUP(); }

    auto epollERR() -> int override {
        error();
        return HANDLED;
    }

    auto cli(bool writeable=true) -> std::shared_ptr<TCPClientStream> {
        std::shared_ptr<TCPClientStream> ret;
        if (!_client.expired()) { 
            ret = _client.lock();
        }
        if (!ret) {
            ret = std::make_shared<TCPClientStream>(peer_name,_fd);
            ret->on_error([this](error_c ec){on_error(ec);});
            _client = ret;
            if (writeable) ret->writeable();
            on_connect(ret, peer_name);
        }
        return ret;
    }

private:
    SockAddrList _addresses;
    SockAddrList::iterator _addr;
    int _fd = -1;
    bool _exists = true;
    IOLoopSvc* _loop;
    std::string peer_name;
    std::unique_ptr<AddressResolver> _resolv;
    std::unique_ptr<AvahiGroup> _group;
    std::unique_ptr<Timer> _timer;
    std::weak_ptr<TCPClientStream> _client;
    inline static Log::Log log {"tcpclient"};
};

#endif  //!__TCPCLI__H__