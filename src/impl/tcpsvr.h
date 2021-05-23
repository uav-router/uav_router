#ifndef __TCPSVR__H__
#define __TCPSVR__H__
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>

#include <chrono>
using namespace std::chrono_literals;

#include "../err.h"
#include "../loop.h"
#include "../log.h"
#include "statobj.h"
#include "yaml.h"

class TCPServerStream : public Client, public IOPollable {
public:
    TCPServerStream(const std::string& name, int fd, IOLoopSvc* loop, std::chrono::nanoseconds stat_period, std::forward_list<std::pair<std::string,std::string>>& tags):IOPollable(name), _fd(fd),_poll(loop->poll()) {
        _poll->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        _cnt = std::make_shared<StatCounters>("tcpsvr");
        _cnt->tags = tags;
        _cnt->tags.push_front({"endpoint",name});
        if (stat_period.count()) {
            loop->stats()->register_report(_cnt, stat_period);
        }
    }
    ~TCPServerStream() override { 
        _exists = false;
        cleanup();
    }
    auto get_peer_name() -> const std::string& override {
        return name;
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
                if (!_exists) return STOP;
                return HANDLED;
            }
            if (sz==0) { 
                error_c ret = _poll->del(_fd, this);
                on_error(ret,"tcp socket cleanup");
                cleanup();
                on_close();
                return STOP;
            }
            void* buffer = alloca(sz);
            int n = recv(_fd, buffer, sz, 0);
            if (n == -1) {
                errno_c ret;
                if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                    on_error(ret, "tcp recv");
                    if (!_exists) return STOP;
                }
            } else {
                if (n != sz) {
                    log.warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                }
                log.debug()<<"on_read"<<std::endl;
                on_read(buffer, n);
                _cnt->add("read",n);
                if (!_exists) return STOP;
            }
        }
        return HANDLED;
    }
    auto epollOUT() -> int override {
        writeable();
        if (!_exists) return STOP;
        return HANDLED;
    }
    auto epollERR() -> int override {
        return HANDLED;
    }
    auto epollHUP() -> int override {
        if (_fd!=-1) {
            error_c ret = _poll->del(_fd, this);
            if (ret) {
                on_error(ret, "loop del");
                if (!_exists) return STOP;
            }
            cleanup();
            on_close();
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
                    _poll->del(_fd,this);
                    cleanup();
                    on_close();
                }
                on_error(ret, "tcp send");
            }
        } else {
            _cnt->add("write",n);
        }
        return n;
    }
private:
    int _fd = -1;
    Poll* _poll;
    bool _is_writeable = true;
    bool _exists = true;
    inline static Log::Log log {"tcpstream"};
    std::shared_ptr<StatCounters> _cnt;
    friend class TcpServerImpl;
};


class TcpServerImpl : public TcpServer, public IOPollable {
public:
    TcpServerImpl(const std::string name, IOLoopSvc* loop):IOPollable(name),_loop(loop),_resolv(loop->address()),_timer(loop->timer()) {
        auto on_err = [this,name](error_c& ec){ on_error(ec,name);};
        _resolv->on_error(on_err);
        _timer->on_error(on_err);
        _timer->shoot([this](){ create(); });
    }
    ~TcpServerImpl() override {
        _exists = false;
        cleanup();
    }
    //TcpServer

#ifdef YAML_CONFIG
    auto init(YAML::Node cfg) -> error_c override {
        auto statcfg = cfg["stat"];
        if (statcfg && statcfg.IsMap()) {
            auto period = duration(statcfg["period"]);
            if (period.count()) {
                stat_period = period;
            }
            auto tags = statcfg["tags"];
            if (tags && tags.IsMap()) {
                for(auto tag : tags) {
                    stat_tags.push_front(make_pair(tag.first.as<std::string>(),tag.second.as<std::string>()));
                }
            }
        }
        int family = address_family(cfg["family"]);
        if (family==AF_UNSPEC) family = AF_INET;
        std::string data;
        if (cfg["interface"]) data = cfg["interface"].as<std::string>();
        if (!data.empty()) interface(data,family);
        data.clear();
        if (cfg["address"]) data = cfg["address"].as<std::string>();
        if (!data.empty()) address(data);
        uint16_t port = 0;
        if (cfg["port"]) port = cfg["port"].as<int>();
        return init(port,family);
    }
#endif

    auto init(uint16_t port, int family) -> error_c override {
        if (!_address.empty()) {
            error_c ec = _addr.init(_address, port);
            if (ec) {
                return _resolv->family(family).socktype(SOCK_STREAM).protocol(IPPROTO_TCP).add_flags(AI_PASSIVE)
                    .init(_address, port, [this,family,port](SockAddrList&& a) {
                        SockAddrList addrlist = std::move(a);
                        if (addrlist.empty() || std::next(addrlist.begin())==addrlist.end()) {
                            _addr = SockAddr::any(family,port);
                            log.warning()<<"Multiple addresses found for "<<_address<<" name. Use ANY address of "<<family<<" family"<<std::endl;
                        } else {
                            _addr = *addrlist.begin();
                        }
                        create();
                    });
            }
        }
        if (_addr.len()==0) { _addr = SockAddr::any(family,port);
        } else { _addr.set_port(port);
        }
        create();
        return error_c();
    }

    void create() {
        error_c ec = create_prim();
        if (on_error(ec,"create TCP server")) {
            if (!_exists) return;
            _timer->arm_oneshoot(3s);
        }
    }

    auto create_prim() -> error_c {
        // start server creation with address
        auto zeroconf = _loop->zeroconf();
        if (!zeroconf) { return create_socket(true);
        }
        if (_addr.port()) {
            auto ret = _loop->zeroconf()->query_service_name(_addr, _addr.family());
            if (!ret.first.empty()) return errno_c(EADDRINUSE, "register service");
        }
        if (_group) {
            create_service(_group.get());
            return error_c();
        }
        _group = _loop->zeroconf()->get_register_group();
        _group->on_create([this](AvahiGroup* g){ create_service(g); 
        });
        _group->on_collision([this](AvahiGroup* g){ 
            log.info()<<"Group collision"<<std::endl;
            g->reset();
            check_collision(g);
        });
        _group->on_established([this](AvahiGroup* g){ 
            log.info()<<"Service "<<name<<" registered"<<std::endl;
            error_c ec = listen_socket();
            if (on_error(ec,"listen")) { 
                if (!_exists) return;
                _timer->arm_oneshoot(3s);
            }
        });
        _group->on_failure([this](error_c ec){ 
            on_error(ec,"Group failure");
            if (!_exists) return;
            _group.reset();
            _timer->arm_oneshoot(3s);
        });
        _group->create();
        return error_c();
    }

    auto create_socket(bool listen) -> error_c {
        if (_fd!=-1) {
            _loop->poll()->del(_fd, this);
            close(_fd);
        }
        _fd = socket(_addr.family(), SOCK_STREAM | SOCK_NONBLOCK, 0);
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
        if (listen) return listen_socket();
        return error_c();
    }

    auto listen_socket() -> error_c {
        error_c ret = err_chk(listen(_fd,5),"listen");
        if (ret) { // && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            _fd = -1;
            return ret;
        }
        return _loop->poll()->add(_fd, EPOLLIN, this);
    }

    void check_collision(AvahiGroup* g) {
        _timer->arm_oneshoot(3s);
    }

    void create_service(AvahiGroup* g) {
        error_c ret = create_socket(false);
        if (on_error(ret)) { 
            if (!_exists) return;
            _timer->arm_oneshoot(3s);
            return;
        }
        SockAddr port_finder(_fd);
        CAvahiService data(name,"_pktstreamnames._tcp");
        if (!_itf_name.empty()) data.itf(_itf_name);
        error_c ec = g->add_service(
            data.family(port_finder.family()),
            port_finder.port()
        );
        if (on_error(ec,"Error adding service "+name)) {
            ec = g->reset();
        } else {
            ec = g->commit();
        }
        if (on_error(ec)) { 
            if (!_exists) return;
            _timer->arm_oneshoot(3s);
        }
    }

    auto address(const std::string& address) -> TcpServer& override {
        _address = address;
        return *this;
    }
    auto interface(const std::string& interface, int family) -> TcpServer& override {
        SockAddrList itf_addr;
        _itf_name = interface;
        if (itf_addr.interface(interface, 0, family)) {
            _addr = _addr = SockAddr::any(family,0);
            log.warning()<<"No address found for "<<interface<<" interface. Use ANY address of "<<family<<" family"<<std::endl;
        } else {
            if (std::next(itf_addr.begin())==itf_addr.end()) {
                _addr = *itf_addr.begin();
            } else {
                _addr = _addr = SockAddr::any(family,0);
                log.warning()<<"Multiple addresses found for "<<interface<<" interface. Use ANY address of "<<family<<" family"<<std::endl;
            }
        }
        return *this;
    }
    //IOPollable

    auto check() -> errno_c {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }

    auto epollIN() -> int override {
        errno_c ret = check();
        if (on_error(ret)) { 
            if (!_exists) return STOP;
            return HANDLED;
        }
        while(true) {
            SockAddr client_addr;
            int client = client_addr.accept(_fd);
            if (client==-1) {
                errno_c ret("accept");
                if (ret!=std::error_condition(std::errc::resource_unavailable_try_again) &&
                    ret!=std::error_condition(std::errc::operation_would_block)) {
                        on_error(ret);
                        if (!_exists) return STOP;
                }
                return HANDLED;
            }
            int yes = 1;
            errno_c ret = err_chk(setsockopt(client,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
            if (ret) { 
                on_error(ret); 
                close(client); 
                if (!_exists) return STOP;
                return HANDLED;
            }
            int flags = fcntl(client, F_GETFL);
            if (flags == -1) { 
                errno_c ret("fcntl getfl"); 
                on_error(ret); 
                close(client); 
                if (!_exists) return STOP;
                return HANDLED;
            }
            ret = fcntl(client, F_SETFL, flags | O_NONBLOCK);
            if (ret) { 
                on_error(ret,"fcntl setfl"); 
                close(client); 
                if (!_exists) return STOP;
                return HANDLED;
            }
            auto zeroconf = _loop->zeroconf();
            std::string name;
            if (zeroconf) {
                auto names = zeroconf->query_service_name(client_addr, SOCK_STREAM);
                name = names.first;    
            }
            if (name.empty()) {
                name = client_addr.format(SockAddr::REG_SERVICE);
            }
            std::shared_ptr<TCPServerStream> cli =
                std::make_shared<TCPServerStream>(name, client, _loop, stat_period, stat_tags);
            cli->on_error([this](error_c ec){on_error(ec);});
            cli->writeable();
            on_connect(cli, name);
        }
        return HANDLED;
    }
    
    auto epollERR() -> int override {
        errno_c ret = check();
        on_error(ret,"sock error");
        if (!_exists) return STOP;
        return HANDLED;
    }

    void cleanup() override {
        if (_fd != -1) {
            close(_fd);
            _fd = -1;
        }
    }

    SockAddr _addr;
    std::string _address;
    std::string _itf_name; 
    int _fd = -1;
    bool _exists = true;
    IOLoopSvc* _loop;
    std::chrono::nanoseconds stat_period = 1s;
    std::forward_list<std::pair<std::string,std::string>> stat_tags;
    std::unique_ptr<AddressResolver> _resolv;
    std::unique_ptr<AvahiGroup> _group;
    std::unique_ptr<Timer> _timer;
    inline static Log::Log log {"tcpserver"};
};


#endif  //!__TCPSVR__H__