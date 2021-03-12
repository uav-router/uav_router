#include <memory>
#include <random>
#include <netinet/in.h>
#include <unistd.h>
//#include <fcntl.h>
#include <cstring>
#include <string>
#include <utility>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#include <map>
#include <set>

#include "err.h"
#include "log.h"
#include "loop.h"
#include "sockaddr.h"
#include "udp.h"

#include "addrinfo.h"
//#include "timer.h"
static Log::Log l("udp");
struct FD {
    int* _fd;
    FD(int& fd): _fd(&fd) {}
    ~FD() {
        if (_fd && *_fd!=-1) {
            close(*_fd);
            *_fd = -1;
        }
    }
    void clear() {_fd=nullptr;}
};
class UdpBase : public IOPollable, public IOWriteable, public error_handler {
public:
    using Streams = std::map<SockAddr, std::shared_ptr<UdpStream>>;
    class UdpStreamImpl final : public UdpStream {
    public:
        UdpStreamImpl(SockAddr addr, UdpBase* s):_addr(std::move(addr)),_socket(s) {
            _socket->on_write_allowed([this](){write_allowed();});
        }
        void on_read(OnReadFunc func) override {
            _on_read = func;
        }
        void on_read(void* buffer, int size) {
            if (_on_read) _on_read(buffer,size);
        }
        auto write(const void* buf, int len) -> int override {
            if (!_socket) return 0;
            return _socket->send(buf,len,_addr);
        }
        void disconnect() {
            _socket = nullptr;
        }
        UdpBase* _socket;
        SockAddr _addr;
        OnReadFunc _on_read;
    };

    UdpBase(const std::string n, bool server):IOPollable(n),_server(server) {}
    void init(addrinfo* ai, bool broadcast=false) {
        _broadcast=broadcast;
        _multicast=false;
        _addr.init(ai);
    }
    void init(SockAddr&& a, bool broadcast=false) {
        _broadcast=broadcast;
        _multicast=false;
        _addr.init(std::move(a));
    }
    void init_multicast(const SockAddr& addr, const SockAddr& itf, unsigned char ttl) {
        _addr = addr;
        _itf = itf;
        _ttl = ttl;
        _broadcast=false;
        _multicast=true;
        l.debug()<<"Multicast addr:"<<_addr<<", itf:"<<_itf<<std::endl;
    }
    void on_read(OnReadFunc func) {
        _on_read = func;
    }
    void cleanup() override {
        for (auto& stream : streams) {
            dynamic_cast<UdpStreamImpl*>(stream.second.get())->disconnect();
        }
        if (_fd != -1) close(_fd);
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
                    }
                } else {
                    if (n != sz) {
                        l.warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                    }
                    l.debug()<<"on_read"<<std::endl;
                    if (_server) {
                        auto stream = streams.find(addr);
                        if (stream==streams.end()) {
                            auto ret = streams.insert( std::make_pair(
                                addr,
                                std::shared_ptr<UdpStream>(new UdpStreamImpl(addr,this))
                            ));
                            if (ret.second) {
                                stream = ret.first;
                                if (_on_connect) _on_connect(stream->second);
                            }
                        }
                        if (stream==streams.end()) {
                            l.error()<<"Insert stream in the map fails"<<std::endl;
                        } else {
                            dynamic_cast<UdpStreamImpl*>(stream->second.get())->on_read(buffer, n);
                        }
                    } else {
                        if (_on_read) _on_read(buffer, n);
                    }
                }
            }
        }
        return HANDLED;
    }
    auto epollOUT() -> int override {
        is_writeable = true;
        write_allowed();
        return HANDLED;
    }

    UdpServer::OnConnectFunc _on_connect;

protected:

    auto send(const void* buf, int len, SockAddr& addr) -> int {
        if (!is_writeable) {
            return 0;
        }
        int ret = sendto(_fd, buf, len, 0, addr.sock_addr(), addr.len());
        if (ret==-1) {
            errno_c err;
            on_error(err, "UDP send datagram");
            is_writeable=false;
        } else if (ret != len) {
            l.error()<<"Partial send "<<ret<<" from "<<len<<" bytes"<<std::endl;
        }
        return ret;
    }

    SockAddr _addr;
    //SockAddr _send_addr;
    SockAddr _itf;
    unsigned char _ttl = 0;
    int _fd = -1;
    bool is_writeable = true;
    OnReadFunc _on_read;
    bool _broadcast = false;
    bool _multicast = false;
    bool _server = false;

    Streams streams;

};

class UdpClientBase : public UdpBase {
public:
    UdpClientBase():UdpBase("udp client",false) {}
    auto start_with(IOLoop* loop) -> error_c override {
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
            in_addr_t itf = _itf.ip4_addr_t();
            if (itf!=htonl(INADDR_ANY)) {
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
    auto write(const void* buf, int len) -> int override {
        return send(buf, len, _addr);
    }
};

std::default_random_engine reng(std::random_device{}());

class UdpServerBase : public UdpBase {
public:
    UdpServerBase():UdpBase("udp server",true) {}
    auto get_port() -> uint16_t { return ports(reng);
    }
    void register_service(AvahiGroup* g, uint16_t port, const std::string& name, std::initializer_list<std::pair<std::string,std::string>> txt = {}) {
        error_c ec = g->add_service(
            CAvahiService(_service_name,"_pktstream._udp").set_ipv4().set_interface(_service_itf),
            port, "", txt 
        );
        if (!ec) {
            ec = g->add_service(
                CAvahiService(name,"_portclaim._udp").set_ipv4().set_interface(_service_itf),
                port
            );
        }
        if (on_error(ec)) { ec = g->reset();
        } else {            ec = g->commit();
        }
        on_error(ec);
    }
    auto create_fd() -> error_c {
        if (_fd!=-1) {
            _loop->del(_fd, this);
            close(_fd);
            _fd = -1;
        }
        _fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_fd == -1) { 
            return errno_c("udp server socket");
        }
        return error_c();
    }
    void create_ordinal_service(AvahiGroup* g) {
        FD watcher(_fd);
        if (on_error(create_fd())) return;
        error_c ret = _addr.bind(_fd);
        if (on_error(ret,"bind")) {
            return;
        }
        ret = _loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        if (on_error(ret,"loop add")) {
            return;
        }
        watcher.clear();
        SockAddr port_finder(_fd);
        uint16_t port = port_finder.port();
        std::string name = _addr.format(SockAddr::Format::REG_SERVICE)+"_"+g->host_name()+".udp";
        register_service(g,port,name);
    }
    void create_service(AvahiGroup* g) {
        if (_loop->service_exists(_service_name)) {
            l.error()<<"Service "<<_service_name<<" exists"<<std::endl;
            return;
        }
        if (! (_multicast || _broadcast)) {
            create_ordinal_service(g);
            return;
        }
        std::string claim_name;
        uint16_t port;
        while (true) {
            port = get_port();
            _addr.set_port(port);
            claim_name = _addr.format(SockAddr::Format::REG_SERVICE)+".udp";
            if (_loop->service_exists(_service_name)) break;
        }
        if (_multicast) { 
            register_service(g, port, claim_name, {{"multicast",_addr.format(SockAddr::Format::IPADDR_ONLY)},{"ttl",std::to_string(_ttl)}});
            return;
        }
        register_service(g, port, claim_name,{{"broadcast",_addr.format(SockAddr::Format::IPADDR_ONLY)}});
    }

    void confirm_create_service() {
        if (! (_multicast || _broadcast)) return;
        FD watcher(_fd);
        if (on_error(create_fd())) return;
        int yes = 1;
        error_c ret;    
        if (_multicast) {
            ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)),"reuse port");
            if (on_error(ret)) {
                ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),"multicast reuseaddr");
                on_error(ret);
            }
            SockAddr any(INADDR_ANY, _addr.port());
            l.debug()<<"Bind server to "<<any<<std::endl;
            ret = any.bind(_fd);
            if (on_error(ret,"bind")) return;
            ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = _addr.ip4_addr_t();
            mreq.imr_interface.s_addr = _itf.ip4_addr_t();
            ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)),"add membership");
        } else {
            ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
            if (on_error(ret)) return;
            ret = _addr.bind(_fd);
        }
        if (on_error(ret)) return;
        ret = _loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        if (on_error(ret,"loop add")) return;
        watcher.clear();
    }

    auto start_with(IOLoop* loop) -> error_c override {
        _loop = loop;
        if (!_service_name.empty()) {
            _group = _loop->get_register_group();
            _group->on_failure([](error_c ec){ 
                l.info()<<"Group error"<<ec<<std::endl;
            });
            _group->on_established([this](AvahiGroup* g){
                l.info()<<"Service registered"<<std::endl;
                confirm_create_service();
            });
            _group->on_create([this](AvahiGroup* g){ 
                create_service(g); 
            });
            _group->on_collision([this](AvahiGroup* g){ 
                l.info()<<"Group collision"<<std::endl;
                g->reset();
                create_service(g);
            });
            _group->create();
            return error_c();
        }
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
                l.warning()<<"Multicast settings: "<<ret<<std::endl;
                ret = err_chk(setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)),"multicast reuseaddr");
                if (ret) return ret;
            }
            SockAddr any(INADDR_ANY, _addr.port());
            l.debug()<<"Bind server to "<<any<<std::endl;
            ret = any.bind(_fd);
            if (ret) return ret;
            ip_mreq mreq;
            mreq.imr_multiaddr.s_addr = _addr.ip4_addr_t();
            mreq.imr_interface.s_addr = _itf.ip4_addr_t();
            ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)),"add membership");
            if (ret) return ret;
        } else {
            error_c ret = _addr.bind(_fd);
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
                mreq.imr_multiaddr.s_addr = _addr.ip4_addr_t();
                mreq.imr_interface.s_addr = _itf.ip4_addr_t();
                error_c ret = err_chk(setsockopt(_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)),"drop membership");
                on_error(ret);
            }
            close(_fd);
            _fd = -1;
        }
    }
    auto write(const void* buf, int len) -> int override {return 0;}

    void set_service_info(const std::string& name, const std::string& itf) {
        _service_name = name;
        _service_itf = itf;
    }
    void set_service_ports_range(uint16_t min, uint16_t max) {
        ports.param(std::uniform_int_distribution<uint16_t>::param_type{min, max});
    }
    void clear_service_info() {
        _service_itf.clear();
        _service_name.clear();
    }    
    std::string _service_name = "";
    std::string _service_itf = "";
    std::uniform_int_distribution<uint16_t> ports;
    std::unique_ptr<AvahiGroup> _group;
    IOLoop* _loop;
};

class UdpClientImpl : public UdpClient {
public:
    UdpClientImpl(std::string  name, IOLoop* loop):_name(std::move(name)),_addr_resolver{AddressResolver::create()},_loop(loop) {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver->on_error(on_err);
        _udp.on_write_allowed([this](){write_allowed();});
    }
    void execute() {
        error_c ec = _udp.start_with(_loop);
        if (!on_error(ec,_name)) {
            _is_writeable = true;
            if (_on_connect) { _on_connect();
            }
        }
    }
    void init(const std::string& host, uint16_t port) override {
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai);
            execute();
        });
        _addr_resolver->remote(host,port,_loop);
    }

    void init_broadcast(uint16_t port, const std::string& interface) override {
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai,true);
            execute();
        });
        _addr_resolver->broadcast(port,_loop,interface);
    }

    void init_multicast(const std::string& address, uint16_t port, const std::string& interface, int ttl) override {
        l.debug()<<"client init multicast started "<<address<<":"<<port<<" i:"<<interface<<std::endl;
        SockAddr maddr(address, port);
        if (maddr.len()==0) {
            errno_c ret(EINVAL);
            on_error(ret,"inet_pton");
            return;
        }
        SockAddr addr;
        _addr_resolver->on_resolve([this,&addr](addrinfo* addr_info) {
            addr.init(addr_info->ai_addr,addr_info->ai_addrlen);
        });

        error_c ret = _addr_resolver->resolve_interface_ip4(interface,port,AddressResolver::Interface::ADDRESS);
        if (ret) { addr.init(INADDR_ANY, port);
        }
        _udp.init_multicast(maddr, addr, ttl);
        execute();
    }

    void init_service(const std::string& service_name, const std::string& interface="") override {
        _query = _loop->get_service_info(service_name, interface);
        _query->on_remove([this](SockAddr addr){ _udp.remove(std::move(addr)); });
        _query->on_ordinal_service([this](SockAddr addr) {
            _udp.init(std::move(addr));
        });
        _query->on_broadcast_service([this](SockAddr addr) {
            _udp.init(std::move(addr),true);
        }
        _query->on_multicast_service([this,interface](SockAddr addr, int ttl) {
            if(!_udp.is_run()) {
                SockAddr ifaddr;
                _addr_resolver->on_resolve([this,&ifaddr](addrinfo* addr_info) {
                    ifaddr.init(addr_info->ai_addr,addr_info->ai_addrlen);
                });
                error_c ret = _addr_resolver->resolve_interface_ip4(interface,addr.port(),AddressResolver::Interface::ADDRESS);
                if (ret) { ifaddr.init(INADDR_ANY, addr.port());
                }
                _udp.init_multicast(addr, ifaddr, ttl);
            }
        }
        _query->run();


        _query = _loop->query_service(CAvahiService(service_name,"_pktstream._udp").set_interface(interface).set_ipv4());
        _query->on_failure([this](error_c ec){on_error(ec,_name);});
        //_query->on_complete([](){});
        _query->on_remove([this](CAvahiService service, AvahiLookupResultFlags flags){
            _udp.epollRDHUP();
        });
        _query->on_resolve([this,interface](CAvahiService service, std::string host_name,
                                SockAddr addr, std::vector<std::pair<std::string,std::string>> txt,
                                AvahiLookupResultFlags flags){
            std::string broadcast_address;
            std::string multicast_address;
            int ttl = 0;
            for (auto& e : txt) {
                if (e.first=="broadcast") {
                    broadcast_address = e.second;
                    break;
                }
                if (e.first=="multicast") {
                    multicast_address = e.second;
                }
                if (e.first=="ttl") {
                    ttl = std::stoi(e.second);
                }
            }
            if (!broadcast_address.empty()) {
                _udp.init(SockAddr(broadcast_address,addr.port()),true);
            } else if (!multicast_address.empty()) {
                // get interface address
                SockAddr ifaddr;
                _addr_resolver->on_resolve([this,&ifaddr](addrinfo* addr_info) {
                    ifaddr.init(addr_info->ai_addr,addr_info->ai_addrlen);
                });

                error_c ret = _addr_resolver->resolve_interface_ip4(interface,addr.port(),AddressResolver::Interface::ADDRESS);
                if (ret) { ifaddr.init(INADDR_ANY, addr.port());
                }
                _udp.init_multicast(SockAddr(multicast_address,addr.port()), ifaddr, ttl);
            } else {
                _udp.init(std::move(addr));
            }
            execute();
        });
    }

    void on_read(OnReadFunc func) override {
        _udp.on_read(func);
    }
    void on_connect(OnEventFunc func) override {
        _on_connect = func;
    }
    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) {
            return 0;
        }
        return _udp.write(buf,len);
    }

    std::string _name;
    IOLoop *_loop;
    UdpClientBase _udp;
    std::unique_ptr<AddressResolver> _addr_resolver;
    std::unique_ptr<AvahiQuery> _query;
    bool _is_writeable = false;
    OnEventFunc _on_connect;
};

auto UdpClient::create(const std::string& name, IOLoop* loop) -> std::unique_ptr<UdpClient> {
    return std::unique_ptr<UdpClient>{new UdpClientImpl(name,loop)};
}

class UdpServerImpl : public UdpServer {
public:
    UdpServerImpl(std::string  name, IOLoop* loop):_name(std::move(name)),_addr_resolver{AddressResolver::create()},_loop(loop) {
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _udp.on_error(on_err);
        _addr_resolver->on_error(on_err);
    };
    void init(uint16_t port, const std::string& host) override {
        _udp.clear_service_info();
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai, false);
            error_c ec = _udp.start_with(_loop);
            _is_writeable = !on_error(ec,"udp server");
        });
        if (_interface.empty()) {
            _addr_resolver->local(port,_loop,host);
        } else {
            _addr_resolver->local_interface(_interface, port,_loop);
        }
    }
    void init_broadcast(uint16_t port) override {
        _udp.clear_service_info();
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai, true);
            error_c ec = _udp.start_with(_loop);
            _is_writeable = !on_error(ec,"udp server");
        });
        _addr_resolver->broadcast(port,_loop,_interface);
    }
    void init_multicast(uint16_t port, const std::string& address) override {
        _udp.clear_service_info();
        l.debug()<<"server init multicast started "<<address<<":"<<port<<" i:"<<_interface<<std::endl;
        SockAddr maddr(address, port);
        if (maddr.len()==0) {
            errno_c ret(EINVAL);
            on_error(ret,"inet_pton");
            return;
        }
        SockAddr addr;
        _addr_resolver->on_resolve([this,&addr](addrinfo* addr_info) {
            addr.init(addr_info->ai_addr,addr_info->ai_addrlen);
        });

        error_c ret = _addr_resolver->resolve_interface_ip4(_interface,port,AddressResolver::Interface::ADDRESS);
        if (ret) { addr.init(INADDR_ANY, port);
        }
        _udp.init_multicast(maddr, addr, 0);
        error_c ec = _udp.start_with(_loop);
        _is_writeable = !on_error(ec,"udp server");
    }
    void init_service(const std::string& service_name) override {
        _udp.set_service_info(service_name,_interface);
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai, false);
            error_c ec = _udp.start_with(_loop);
            _is_writeable = !on_error(ec,"udp server");
        });
        _addr_resolver->local_interface(_interface, 0,_loop);
    }
    void init_broadcast_service(const std::string& service_name) override {
        _udp.set_service_info(service_name,_interface);
        _addr_resolver->on_resolve([this](addrinfo* ai) {
            _udp.init(ai, true);
            error_c ec = _udp.start_with(_loop);
            _is_writeable = !on_error(ec,"udp server");
        });
        _addr_resolver->broadcast(0,_loop,_interface);
    }
    void init_multicast_service(const std::string& service_name, const std::string& address, int ttl) override {
        _udp.set_service_info(service_name,_interface);
        SockAddr maddr(address, 0);
        if (maddr.len()==0) {
            errno_c ret(EINVAL);
            on_error(ret,"inet_pton");
            return;
        }
        SockAddr addr;
        _addr_resolver->on_resolve([this,&addr](addrinfo* addr_info) {
            addr.init(addr_info->ai_addr,addr_info->ai_addrlen);
        });

        error_c ret = _addr_resolver->resolve_interface_ip4(_interface,0,AddressResolver::Interface::ADDRESS);
        if (ret) { addr.init(INADDR_ANY, 0);
        }
        _udp.init_multicast(maddr, addr, ttl);
        error_c ec = _udp.start_with(_loop);
        _is_writeable = !on_error(ec,"udp server");
    }

    auto set_interface(const std::string& interface) -> UdpServer& override {
        _interface = interface;
        return *this;
    }
    auto set_service_port_range(uint16_t port_min,uint16_t port_max) -> UdpServer& override {
        _udp.set_service_ports_range(port_min, port_max);
        return *this;
    }

    void on_connect(OnConnectFunc func) override {
        _udp._on_connect = func;
    }

    std::string _name;
    std::string _interface = "";
    IOLoop *_loop;
    std::unique_ptr<AddressResolver> _addr_resolver;
    UdpServerBase _udp;
    bool _is_writeable = false;
};

auto UdpServer::create(const std::string& name, IOLoop* loop) -> std::unique_ptr<UdpServer> {
    return std::unique_ptr<UdpServer>{new UdpServerImpl(name,loop)};
}