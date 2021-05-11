#ifndef __UDPCLI__H__
#define __UDPCLI__H__
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <cstring>
#include <map>
#include <chrono>
using namespace std::chrono_literals;

#include "fd.h"
#include "statobj.h"
#include "../err.h"
#include "../loop.h"
#include "../log.h"



class UDPClientStream: public Client {
public:
    UDPClientStream(const std::string& name):_name(name) {}
    
    auto write(const void* , int) -> int override { return -1;
    }

    auto get_peer_name() -> const std::string& override {
        return _name;
    }

    const std::string& _name;

    friend class UdpClientImpl;
};

class UdpClientImpl : public UdpClient, public IOPollable, public ServiceEvents {
public:
    UdpClientImpl(const std::string name, IOLoopSvc* loop):IOPollable(name),_loop(loop),_resolv(loop->address()),_timer(loop->timer()) {
        //_timer->shoot([this](){ connect(); });
        auto on_err = [this,name](error_c& ec){ on_error(ec,name);};
        _resolv->on_error(on_err);
        _timer->on_error(on_err);
        _cnt = std::make_shared<StatCounters>("udpcli");
        _cnt->tags.push_front({"endpoint",name});
        loop->register_report(_cnt, 1s);
    }

    ~UdpClientImpl() override {
        _exists = false;
        if (_fd != -1) {
            _loop->poll()->del(_fd, this);
            for (auto& stream : _streams) {
                auto cli = stream.second.lock();
                if (cli) { cli->on_close();
                }
            }
            on_error(err_chk(close(_fd),"close"));
            _fd = -1;
        }
    }

    auto init(const std::string& host, uint16_t port, int family=AF_UNSPEC) -> error_c override {
        if (_addr.init(host,port)) { return create(SockAddr::any(_addr.family()));
        }
        return _resolv->family(family).socktype(SOCK_DGRAM).protocol(IPPROTO_UDP)
            .init(host, port, [this](SockAddrList&& a) {
                SockAddrList addresses = std::move(a);
                if (addresses.begin()!=addresses.end()) {
                    _addr = *addresses.begin();
                    error_c ec = create(SockAddr::any(_addr.family()));
                    on_error(ec);
                }
        });
    }

    void from_service_descr() {
        log.debug()<<"from_service_descr "<<_service_name<<std::endl;
        std::vector<std::pair<std::string,std::string>> txt;
        SockAddrList addresses;
        auto zeroconf = _loop->zeroconf();
        if (zeroconf->service_info(_service_name, addresses, txt)) {
            std::string broadcast_addr;
            std::string multicast_addr;
            int ttl = 0;
            error_c ec;
            for(auto& rec: txt) {
                if (rec.first=="broadcast") {        broadcast_addr = rec.second;
                } else if (rec.first=="multicast") { multicast_addr = rec.second;
                } else if (rec.first=="ttl") {       ttl = std::stoi(rec.second);
                }
            }
            if (!broadcast_addr.empty()) {
                _addr.init(broadcast_addr,addresses.begin()->port());
                ec = create(SockAddr::any(AF_INET),true);
            } else if (!multicast_addr.empty()) {
                _addr.init(multicast_addr,addresses.begin()->port());
                ip_mreqn req;
                memset(&req,0,sizeof(req));
                ec = create(SockAddr::any(_addr.family()),false,&req,ttl);
            } else {
                _addr = *addresses.begin();
                ec = create(SockAddr::any(_addr.family()));
            }
            on_error(ec);
        }
    }

    void svc_resolved(std::string name, std::string endpoint, int itf, const SockAddr& addr) override {
        log.debug()<<"svc_resolved"<<std::endl;
        if (_fd!=-1) return;
        if (name==_service_name || endpoint==_service_name) {
            if (_itf.second && _itf.second!=itf) return;
            from_service_descr();
        }
    }
    void svc_removed(std::string name) override {
    }

    auto init_service(const std::string& service_name, const std::string& interface="") -> error_c override {
        _service_name = service_name;
        if (!interface.empty()) {
            _itf = itf_from_str(interface);
        }
        auto zeroconf = _loop->zeroconf();
        if (!zeroconf) return errno_c(EPROTONOSUPPORT, "Zeroconf not available");
        if (!_service_pollable) {
            _service_pollable = std::make_shared<ServicePollableProxy>(this);
            _loop->zeroconf()->watch_services(_service_pollable, SOCK_DGRAM);
        }
        from_service_descr();
        return error_c();
    }

    auto init_broadcast(uint16_t port, const std::string& interface="") -> error_c override {
        SockAddr local = SockAddr::any(AF_INET);
        if (interface.empty()) { 
            _addr.init("<broadcast>",port);
        } else {
            if (_addr.init(interface,port)) {
                local = SockAddr::local(_addr.itf(true),AF_INET);
            } else {
                auto itf = itf_from_str(interface);
                SockAddrList list;
                list.broadcast(itf.first, port);
                if (list.empty()) {
                    log.warning()<<"No address found for interface "<<itf.first<<". Use global broadcast address"<<std::endl;
                    _addr.init("<broadcast>",port);
                } else if (std::next(list.begin())!=list.end()) {
                    log.warning()<<"Multiple address found for interface "<<itf.first<<". Use global broadcast address"<<std::endl;
                    _addr.init("<broadcast>",port);
                } else {
                    _addr = *list.begin();
                    local = SockAddr::local(_addr.itf(true),AF_INET);
                }
            }
            
        }
        return create(local,true);
    }

    auto init_multicast(const std::string& address, uint16_t port, const std::string& interface="", uint8_t ttl = 0) -> error_c override {
        error_c ret = _addr.init(address,port);
        if (ret) {
            ret.add_place("multicast address");
            return ret;
        }
        ip_mreqn req;
        memset(&req,0,sizeof(req));
        SockAddr local;
        if (interface.empty()) {
            local = SockAddr::any(_addr.family());
        } else {
            std::string itf_name = interface;
            SockAddr itf_addr;
            if (itf_addr.init(interface)) {
                if (_addr.family()!=itf_addr.family()) {
                    return error_c(EAFNOSUPPORT, std::system_category(), "itf and multicast difference");
                }
                itf_name = itf_addr.itf();
                if (itf_name.empty()) {
                    return error_c(ENODEV, std::system_category(), "no itf with address");
                }
                local = itf_addr;
            }
            auto itf = itf_from_str(itf_name);
            if (itf.second==0) {
                errno_c ec("if_nametoindex");
                return ec;
            }
            req.imr_ifindex = itf.second;
            if (local.len()==0) {
                local = SockAddr::local(itf.first, _addr.family());    
            }
        }
        return create(local,false,&req,ttl);
    }
    
    auto create(const SockAddr& local, bool broadcast = false, ip_mreqn* itf = nullptr, uint8_t ttl = 0) -> error_c {
        FD watcher(_fd);
        int family = local.family();
        _fd = socket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp client socket");
        }
        if (broadcast) {
            int yes = 1;
            errno_c ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_BROADCAST, (void *) &yes, sizeof(yes)),"setsockopt(broadcast)");
            if (ret) return ret;
        } else if (itf) { // multicast socket
            if (family==AF_INET) {
                if (ttl) {
                    errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&ttl, sizeof(ttl)),"multicast ttl");
                    if (ret) return ret;
                }
                if (itf->imr_address.s_addr!=htonl(INADDR_ANY) || itf->imr_ifindex) {
                    errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_MULTICAST_IF, itf, sizeof(*itf)),"multicast itf");
                    if (ret) return ret;
                }
            } else {
                if (ttl) {
                    errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (void *)&ttl, sizeof(ttl)),"multicast ttl");
                    if (ret) return ret;
                }
                
                if (itf->imr_ifindex) {
                    errno_c ret = err_chk(setsockopt(_fd, IPPROTO_IPV6, IPV6_MULTICAST_IF, &itf->imr_ifindex, sizeof(itf->imr_ifindex)),"multicast itf");
                    if (ret) return ret;
                }
            }
        }
        if (_loop->zeroconf()) {
            SockAddr local = SockAddr::any(_addr.family());
            error_c ec = local.bind(_fd);
            if (ec) return ec;
            SockAddr myaddr(_fd);
            if (!_group) {
                _group = _loop->zeroconf()->get_register_group();
            } else {
                _group->reset();
            }
            _group->on_create([this, port = myaddr.port(), family=myaddr.family(), itf = myaddr.itf()](AvahiGroup* g){
                auto svc = CAvahiService(name,"_pktstreamnames._udp").family(family);
                if (!itf.empty()) { svc.itf(itf);
                }
                error_c ec = g->add_service(svc, port);
                if (ec) {
                    on_error(ec,"add service");
                    ec = g->reset();
                    on_error(ec,"reset service");
                    ec = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
                    on_error(ec,"poll add create service reset");
                    writeable();
                } else {
                    ec = g->commit();
                    on_error(ec,"commit service");
                }
            });
            _group->on_collision([this](AvahiGroup* g){ 
                log.error()<<"Collision on endpoint name "<<name<<std::endl;
                g->reset();
                auto ec = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
                on_error(ec,"poll add collision");
            });
            _group->on_established([this](AvahiGroup* g){ 
                log.info()<<"Service "<<name<<" registered"<<std::endl;
                _timer->shoot([this](){ 
                    auto ec = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
                    on_error(ec,"poll add established");
                 }).arm_oneshoot(500ms); // wait for service info propagation
                
            });
            _group->on_failure([this](error_c ec){ 
                on_error(ec,"registration failure");
                ec = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
                on_error(ec,"poll add failure");
            });
            _group->create();
            watcher.clear();
            return error_c();
        }
        auto ret = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        if (ret) return ret;
        watcher.clear();
        writeable();
        return error_c();
    }
    auto epollIN() -> int override {
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
                SockAddr addr;
                ssize_t n = recvfrom(_fd, buffer, sz, 0, addr.sock_addr(), &addr.size());
                if (n<0) {
                    errno_c ret;
                    if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                        on_error(ret, "udp recvfrom");
                    } else break;
                } else {
                    if (n != sz) {
                        log.warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    std::string name;
                    if (_loop->zeroconf()) {
                        name = _loop->zeroconf()->query_service_name(addr, SOCK_DGRAM).second;
                    }
                    if (name.empty()) {
                        name = addr.format(SockAddr::REG_SERVICE);
                    }
                    auto& stream = _streams[name];
                    if (stream.expired()) {
                        auto cli = std::make_shared<UDPClientStream>(name);
                        stream = cli;
                        on_connect(cli,name);
                        if (!_exists) return STOP;
                    }
                    dynamic_cast<UDPClientStream*>(stream.lock().get())->on_read(buffer, n);
                    _cnt->add("read",n);
                    if (!_exists) return STOP;
                }
            }
        }
        return HANDLED;
    }
    auto epollOUT() -> int override {
        writeable();
        if (!_exists) return STOP;
        return HANDLED;
    }

    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) {
            return -1;
        }
        int ret = sendto(_fd, buf, len, 0, _addr.sock_addr(), _addr.len());
        if (ret==-1) {
            errno_c err;
            on_error(err, "UDP send datagram");
            _is_writeable=false;
        } else {
            _cnt->add("write",ret);
            if (ret != len) {
                log.error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
            }
        }
        return ret;
    }

private:
    SockAddr _addr;
    int _fd = -1;
    bool _exists = true;
    std::string _service_name;
    std::pair<std::string,int> _itf = {"",0};
    std::shared_ptr<ServiceEvents> _service_pollable;
    IOLoopSvc* _loop;
    std::unique_ptr<AddressResolver> _resolv;
    std::unique_ptr<Timer> _timer;
    std::unique_ptr<AvahiGroup> _group;
    std::map<std::string, std::weak_ptr<UDPClientStream>> _streams;
    std::shared_ptr<StatCounters> _cnt;
    inline static Log::Log log {"udpclient"};
};

#endif  //!__UDPCLI__H__