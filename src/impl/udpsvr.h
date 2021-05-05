#ifndef __UDPSVR__H__
#define __UDPSVR__H__

#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <random>
#include <map>
#include <cstring>
#include <sys/socket.h>

#include "fd.h"
#include "../err.h"
#include "../loop.h"
#include "../log.h"

std::default_random_engine reng(std::random_device{}());

class UDPServerStream: public Client {
public:
    UDPServerStream(const std::string& name, int fd, SockAddr addr):_name(name),_fd(fd), _addr(std::move(addr)) {
        writeable();
    }
    
    auto write(const void* buf, int len) -> int override { 
        if (!_is_writeable) {
            return -1;
        }
        if (_fd==-1) {
            return -1;
        }
        int ret = sendto(_fd, buf, len, 0, _addr.sock_addr(), _addr.len());
        if (ret==-1) {
            errno_c err;
            on_error(err, "UDP send datagram");
            _is_writeable=false;
        } /*else if (ret != len) {
            log.error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }*/
        return ret;
    }

    auto get_peer_name() -> const std::string& override {
        return _name;
    }

    void on_close() override { 
        Closeable::on_close();
        _fd = -1;
        _is_writeable = false;
    }

    const std::string& _name;
    int _fd = -1;
    SockAddr _addr;

    friend class UdpServerImpl;
};

class UdpServerImpl : public UdpServer, public IOPollable, public ServiceEvents {
public:
    UdpServerImpl(const std::string name, IOLoopSvc* loop):IOPollable(name),_loop(loop) {
    }
    ~UdpServerImpl() override {
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
    auto address(const std::string& address) -> UdpServer& override {
        _address = address;
        return *this;
    }

    auto interface(const std::string& interface) -> UdpServer& override {
        _itf = itf_from_str(interface);
        if (_itf.second == 0) {
           log.warning()<<"Can't recognize value '"<<interface<<"' as interface"<<std::endl;
        }
        return *this;
    }

    auto service_port_range(uint16_t min, uint16_t max) -> UdpServer& override {
        _ports.param(std::uniform_int_distribution<uint16_t>::param_type{min, max});
        return *this;
    }

    auto define_port(SockAddr& addr) -> uint16_t {
        auto zeroconf = _loop->zeroconf();
        while (true) {
            uint16_t port = _ports(reng);
            if (!_loop->zeroconf()->port_claimed(port,addr)) return port;
            //TODO: endless loop
        }
    }

    auto ttl(uint8_t ttl_) -> UdpServer& override {
        _ttl = ttl_;
        return *this;
    }

    auto setup_fd(uint16_t port, Mode mode, SockAddr &addr) -> error_c {
        if (_fd!=-1) {
            ::close(_fd);
            _fd = -1;
            _loop->poll()->del(_fd, this);
        }
        FD watcher(_fd);
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { return errno_c("udp server socket");
        }
        
        if (mode == UNICAST) {
            if (!_address.empty()) {
                if (addr.init(_address,port)) {
                    log.warning()<<"Can't recognize value '"<<_address<<"' as address"<<std::endl;
                } else if (addr.itf().empty()) {
                    log.warning()<<"Address "<<addr<<" is not a local interface address"<<std::endl;
                    //return errno_c(EADDRNOTAVAIL);
                }
            }
            if (!addr.len() && _itf.second) {
                if ((_family==AF_INET) || (_family==AF_INET6)) {
                    addr = SockAddr::local(_itf.first,_family,port);
                    if (!addr.len()) { log.warning()<<"Can't get address of family "<<_family<<" for interface "<<_itf.first<<std::endl;
                    }
                } else {
                    log.warning()<<"Family "<<_family<<" not supported"<<std::endl;
                }
            }
            if (!addr.len()) addr = SockAddr::any(_family, port);
            if (!addr.len()) {
                log.error()<<"Can't detect address to hear"<<std::endl;
                return errno_c(EADDRNOTAVAIL);
            }
            error_c ret = addr.bind(_fd);
            if (!ret && addr.port()==0) {
                addr.init(_fd);
            }
            if (ret) return ret;
        } else if (mode == BROADCAST) {
            if (!_address.empty()) {
                if (addr.init(_address,port)) {
                    log.warning()<<"Can't recognize value '"<<_address<<"' as address"<<std::endl;
                } else if (addr.itf(true).empty()) {
                    log.warning()<<"Address "<<addr<<" is not a local interface broadcast address"<<std::endl;
                    //return errno_c(EADDRNOTAVAIL);
                }
            }
            if (!addr.len() && _itf.second) {
                addr = SockAddr::broadcast(_itf.first, port);
                if (!addr.len()) { log.warning()<<"Can't get address of family "<<_family<<" for interface "<<_itf.first<<std::endl;
                }
            }
            if (!addr.len()) addr.init("<broadcast>", port);
            if (!addr.len()) {
                log.error()<<"Can't detect address to listen"<<std::endl;
                return errno_c(EADDRNOTAVAIL);
            }
            if (port==0) {
                port = define_port(addr);
                addr.set_port(port);
            }
            int yes = 1;
            error_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
            if (ret) return ret;
            ret = addr.bind(_fd);
            if (ret) return ret;
        } else { // MULTICAST
            if (_address.empty()) {
                return errno_c(EADDRNOTAVAIL,"Multicast address");
            }
            error_c ec = addr.init(_address,port);
            if (ec) { return ec;                
            }
            if (port==0) {
                port = define_port(addr);
                addr.set_port(port);
            }
            int yes = 1;
            error_c ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)),"reuse port");
            if (ret) {
                log.warning()<<"Multicast settings: "<<ret<<std::endl;
                ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),"multicast reuseaddr");
                if (ret) return ret;
            }
            SockAddr any = SockAddr::any(addr.family(), port);
            ret = any.bind(_fd);
            if (ret) return ret;
            if (addr.family()==AF_INET) {
                ip_mreqn mreq;
                memset(&mreq,0,sizeof(mreq));
                mreq.imr_multiaddr.s_addr = addr.ip4_addr_t();
                mreq.imr_ifindex = _itf.second;
                ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)),"add membership");
                if (ret) return ret;
            } else { // AF_INET6
                ipv6_mreq mreq;
                memcpy(mreq.ipv6mr_multiaddr.s6_addr,addr.ip6_addr(),sizeof(in6_addr));
                mreq.ipv6mr_interface = _itf.second;
                ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)),"add membership");
                if (ret) return ret;
            }
        }
        watcher.clear();
        return error_c();
    }

    void register_service(AvahiGroup* g, uint16_t port, int family, const std::string& name, std::initializer_list<std::pair<std::string,std::string>> txt = {}) {
        auto svc = CAvahiService(name,"_pktstreamnames._udp").family(family);
        if (_itf.second) svc.itf(_itf.second);
        error_c ec = g->add_service(svc, port, "", txt);
        if (on_error(ec)) { ec = g->reset();
        } else {            ec = g->commit();
        }
        on_error(ec);
    }

    void create_service(AvahiGroup* g, Mode mode, SockAddr addr) {
        if (mode==UNICAST) {
            register_service(g,addr.port(),addr.family(),name);
        } else if (mode==BROADCAST) {
            register_service(g,addr.port(),AF_INET,addr.format(SockAddr::REG_SERVICE,"-claim"),{
                {"endpoint",name},
                {"broadcast",addr.format(SockAddr::IPADDR_ONLY)}
            });
        } else { //MULTICAST
            register_service(g,addr.port(),addr.family(),addr.format(SockAddr::REG_SERVICE,"-claim"),{
                {"endpoint",name},
                {"multicast",addr.format(SockAddr::IPADDR_ONLY)},
                {"ttl",std::to_string(_ttl)}
            });
        }
    }

    auto init(uint16_t port=0, Mode mode = UNICAST) -> error_c override {
        if (!_loop->zeroconf() && port==0) return errno_c(EINVAL,"Zero port");
        SockAddr addr;
        error_c ret = setup_fd(port,mode, addr);
        if (ret) return ret;
        if (!_loop->zeroconf()) {
            ret = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
            if (ret) return ret;
            return error_c();
        }
        if (!_group) {
            _group = _loop->zeroconf()->get_register_group();
        } else {
            _group->reset();
        }
        _group->on_failure([this](error_c ec){ 
            on_error(ec);
        });
        _group->on_established([this](AvahiGroup* g){
            error_c ret = _loop->poll()->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
            on_error(ret);
        });
        _group->on_create([this,mode,addr](AvahiGroup* g){ 
            create_service(g,mode,addr); 
        });
        _group->on_collision([this,port,mode](AvahiGroup* g){ 
            log.info()<<"Group collision"<<std::endl;
            g->reset();
            error_c ret = init(port,mode);
            on_error(ret);
        });
        _group->create();
        if (!_service_pollable) {
            _service_pollable = std::make_shared<ServicePollableProxy>(this);
            _loop->zeroconf()->watch_services(_service_pollable, SOCK_DGRAM);
        }
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
                        name = _loop->zeroconf()->query_service_name(addr, SOCK_DGRAM).first;
                    }
                    if (name.empty()) {
                        name = addr.format(SockAddr::REG_SERVICE);
                    }
                    auto& stream = _streams[name];
                    if (stream.expired()) {
                        auto cli = std::make_shared<UDPServerStream>(name,_fd, std::move(addr));
                        stream = cli;
                        on_connect(cli,name);
                        if (!_exists) return STOP;
                    }
                    dynamic_cast<UDPServerStream*>(stream.lock().get())->on_read(buffer, n);
                    if (!_exists) return STOP;
                }
            }
        }
        return HANDLED;
    }
    auto epollOUT() -> int override {
        for (auto& stream : _streams) {
            auto cli = stream.second.lock();
            if (cli) { 
                cli->writeable();
                if (!_exists) return STOP;
            }
        }
        return HANDLED;
    }

    void svc_resolved(std::string name, std::string endpoint, int itf, const SockAddr& addr) override {
    }
    void svc_removed(std::string name) override {
        // we have to close client stream when the service gone
        auto stream_it = _streams.find(name);
        if (stream_it!=_streams.end()) {
            auto cli = stream_it->second.lock();
            if (cli) { 
                cli->on_close(); 
                _streams.erase(name);
            }
        }
    }
    
private:
    std::string _address;
    std::pair<std::string,int> _itf = {"",0};
    uint8_t _ttl = 0;
    int _family = AF_INET;
    std::uniform_int_distribution<uint16_t> _ports;
    int _fd = -1;
    bool _exists = true;
    IOLoopSvc* _loop;
    std::map<std::string, std::weak_ptr<UDPServerStream>> _streams;
    std::unique_ptr<AvahiGroup> _group;
    std::shared_ptr<ServiceEvents> _service_pollable;
    inline static Log::Log log {"udpserver"};
};

#endif  //!__UDPSVR__H__