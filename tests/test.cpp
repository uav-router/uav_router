#include <unistd.h>
#include <ifaddrs.h>

#include <sys/ioctl.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <iostream>

#include <chrono>
using namespace std::chrono_literals;

#include <cstring>

#include <log.h>
#include <err.h>
#include <epoll.h>
#include <addrinfo.h>
#include <timer.h>
#include <udp.h>
#include <tcp.h>


//g++ test.cpp src/udp.cpp src/timer.cpp src/err.cpp src/epoll.cpp src/log.cpp src/addrinfo.cpp -I src -o test -lanl

void print_addr(const struct sockaddr *sa, socklen_t salen) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    eai_code ec = getnameinfo(sa, salen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ec) {
       log::error()<<"Get nameinfo error: "<<ec.message()<<" family="<<sa->sa_family<<" len="<<salen<<std::endl;
    } else {
        log::info()<<"address = "<<host<<":"<<port<<std::endl;
    }
}

void addr_info_result(addrinfo* addr, const std::error_code& res) {
    if (res) {
        log::error()<<"Get address error "<<": "<<res.message()<<std::endl;
    } else {
        char host[NI_MAXHOST];
        char port[NI_MAXSERV];
        while(addr) {
            eai_code ec = getnameinfo(addr->ai_addr, addr->ai_addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
            if (ec) {
               log::error()<<"Get nameinfo error: "<<ec.message()<<std::endl;
            } else {
                log::info()<<"address = "<<host<<":"<<port
                <<" family "<<addr->ai_family<<" socktype "<<addr->ai_socktype
                <<" protocol "<<addr->ai_protocol<<" flags "<<addr->ai_flags;
                if (addr->ai_canonname) log::info()<<" canonname "<<addr->ai_canonname;
                log::info()<<std::endl;
            }
            addr = addr->ai_next;
        }
    }
}

int addrinfo_test() {
    AddrInfo request;
    //request.init("localhost","2020",AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    request.init("localhost","2020",0,0,0,AI_PASSIVE | AI_CANONNAME);
    IOLoop loop;
    loop.execute(&request);
    request.on_result_func(addr_info_result);
    loop.run();
    return 0;
}

int timer_test() {
    Timer request;
    request.init_periodic(2s);
    IOLoop loop;
    loop.execute(&request);
    request.on_shoot_func([] () {std::cout<<"Timer event"<<std::endl;});
    loop.run();
    return 0;
}

/*int udp_client_base_test() {
    AddrInfo request;
    request.init("localhost","20001",AF_INET);
    IOLoop loop;
    UdpClientBase socket;
    loop.execute(&request);
    request.on_result_func([&loop,&socket](addrinfo* addr, const std::error_code& res) {
        if (res) {
            std::cout<<"Address resolution error: "<<res.message()<<std::endl;
        } else {
            socket.init(addr->ai_addrlen, addr->ai_addr);
            loop.execute(&socket);
            socket.on_read_func([](void* buf, int len) {
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            socket.write("Hello!",6);
        }
    });
    loop.run();
    return 0;
}*/

#include <sys/epoll.h>

class TcpServerBase : public IOPollable {
public:
    using OnConnectFunc = std::function<void(int, sockaddr*, socklen_t)>;
    void init(socklen_t addrlen, sockaddr *addr) {
        _addrlen = addrlen;
        memcpy(&_addr,addr,_addrlen);
    }
    void on_connect_func(OnConnectFunc func) {
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
            if (ret) { on_error(ret);
            } else while(true) {
                sockaddr_in client_addr;
                socklen_t ca_len = sizeof(client_addr);
                int client = accept(_fd, (sockaddr *) &client_addr, &ca_len);
                if (client==-1) {
                    errno_c ret("accept");
                    if (ret!=std::error_condition(std::errc::resource_unavailable_try_again) &&
                        ret!=std::error_condition(std::errc::operation_would_block)) {
                            on_error(ret);
                    }
                } else {
                    on_connect(client, (struct sockaddr *) &client_addr, ca_len);
                }
            }
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
        if (_fd == -1) { return errno_c("tcp server socket");
        }
        int yes = 1;
        //TODO: SO_PRIORITY SO_RCVBUF SO_SNDBUF SO_RCVLOWAT SO_SNDLOWAT SO_RCVTIMEO SO_SNDTIMEO SO_TIMESTAMP SO_TIMESTAMPNS SO_INCOMING_CPU
        /*errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)),"reuseaddr");
        if (ret) {
            close(_fd);
            return ret;
        }*/
        errno_c ret = err_chk(setsockopt(_fd,SOL_SOCKET,SO_KEEPALIVE,&yes,sizeof(yes)),"keepalive");
        if (ret) {
            close(_fd);
            return ret;
        }
        ret = err_chk(bind(_fd,(sockaddr*)&_addr, _addrlen),"connect");
        if (ret) { // && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            return ret;
        }

        ret = err_chk(listen(_fd,5),"listen");
        if (ret) { // && ret!=std::error_condition(std::errc::operation_in_progress)) {
            close(_fd);
            return ret;
        }
        return loop->add(_fd, EPOLLIN, this);
    }

    void on_connect(int socket, sockaddr* addr, socklen_t len) {
        if (_on_connect) {
            _on_connect(socket, addr, len);
        } else {
            log::warning()<<"No socket handler"<<std::endl;
            close(socket);
        }
    }

private:
    socklen_t _addrlen = 0;
    sockaddr_storage _addr;
    int _fd = -1;
    bool is_writeable = false;
    bool is_connected = false;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    OnConnectFunc _on_connect;
};

class TcpSocket : public IOPollable, public IOWriteable {
public:
    void init(int fd) {
        _fd = fd;
    }
    errno_c check() {
        int ec;
        socklen_t len = sizeof(ec);
        errno_c ret = err_chk(getsockopt(_fd, SOL_SOCKET, SO_ERROR, &ec, &len),"getsockopt");
        if (ret) return ret;
        return errno_c(ec,"socket error");
    }
    void events(IOLoop* loop, uint32_t evs) override {
        log::debug()<<"tcp socket event "<<evs<<std::endl;
        errno_c err = check();
        if (err) {
            on_error(err,"socket error");
        }
        if (evs & EPOLLIN) {
            evs &= ~EPOLLIN;
            log::debug()<<"EPOLLIN"<<std::endl;
            while(true) {
                int sz;
                errno_c ret = err_chk(ioctl(_fd, FIONREAD, &sz),"tcp ioctl");
                if (ret) {
                    on_error(ret, "Query data size error");
                } else {
                    if (sz==0) { 
                        break;
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
                            log::warning()<<"Datagram declared size "<<sz<<" is differ than read "<<n<<std::endl;
                        }
                        log::debug()<<"on_read"<<std::endl;
                        if (_on_read) _on_read(buffer, n);
                    }
                }
            }
        }
        if (evs & EPOLLOUT) {
            evs &= ~EPOLLOUT;
            log::debug()<<"EPOLLOUT"<<std::endl;
            _is_writeable = true;
        }
        if (evs & EPOLLERR) {
            evs &= ~EPOLLERR;
            log::debug()<<"EPOLLERR"<<std::endl;
            on_error(err,"socket error");
        }
        if (evs & EPOLLHUP) {
            evs &= ~EPOLLHUP;
            log::debug()<<"EPOLLHUP"<<std::endl;
            if (_on_close) { _on_close();
            }
            error_c ret = _loop->del(_fd, this);
            if (ret) on_error(ret, "loop del");
            cleanup();
        }
        if (evs) {
            log::warning()<<"TCP socket unexpected event "<<evs<<std::endl;
        }
    }
    error_c start_with(IOLoop* loop) override {
        _loop = loop;
        return loop->add(_fd, EPOLLIN | EPOLLOUT, this);
    }
    void cleanup() override {
        if (_fd != -1) {
            close(_fd);
        }
    }
    int write(const void* buf, int len) {
        if (!_is_writeable) return 0;
        ssize_t n = send(_fd, buf, len, 0);
        _is_writeable = n==len;
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                on_error(ret, "tcp send");
            }
        }
    }
private:
    int _fd = -1;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    IOLoop* _loop;
    bool _is_writeable = true;
};

/*int udp_server_base_test() {
    IOLoop loop;
    UdpServerBase udp;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family    = AF_INET; // IPv4 
    servaddr.sin_addr.s_addr = INADDR_ANY; 
    servaddr.sin_port = htons(20001);
    udp.init(sizeof(servaddr), (sockaddr*)&servaddr);
    udp.on_read_func([&udp](void* buf, int len){
        udp.write(buf,len);
    });
    loop.execute(&udp);
    loop.run();
    return 0;
}*/

int udp_client_test() {
    IOLoop loop;
    UdpClient udp("MyEndpoint");
    udp.init("localhost",20001, &loop);
    udp.on_read_func([](void* buf, int len){
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    udp.on_connect_func([&udp]() {
        std::cout<<"on connect "<<udp.write("Hello!", 6)<<std::endl;
    });
    udp.on_error_func([&udp](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    loop.run();
    return 0;
}

int udp_server_test() {
    IOLoop loop;
    UdpServer udp("MyEndpoint");
    udp.init(20001, &loop);
    udp.on_read_func([&udp](void* buf, int len){
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
        const char* answ = "Hello from server!";
        udp.write(answ, strlen(answ));
    });
    udp.on_error_func([&udp](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    loop.run();
    return 0;
}

int tcp_client_test() {
    IOLoop loop;
    TcpClient tcp("MyEndpoint");
    tcp.init("192.168.0.25",10000,&loop);
    tcp.on_error_func([&tcp](const error_c& ec) {
        std::cout<<"Tcp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    tcp.on_read_func([](void* buf, int len) {
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    tcp.on_connect_func([&tcp]() {
        std::cout<<"socket connected"<<std::endl;
        tcp.write("Hello!",6);
    });
    tcp.on_close_func([]() {
        std::cout<<"socket disconnected"<<std::endl;
    });
    loop.run();
    return 0;
}

/*int test_tcp_client_base() {
    AddrInfo request;
    request.init("192.168.0.25","10000",AF_INET);
    IOLoop loop;
    TcpClientBase socket;
    loop.execute(&request);
    request.on_result_func([&loop,&socket](addrinfo* addr, const std::error_code& res) {
        if (res) {
            std::cout<<"Address resolution error: "<<res.message()<<std::endl;
        } else {
            socket.init(addr->ai_addrlen, addr->ai_addr);
            loop.execute(&socket);
            socket.on_read_func([](void* buf, int len) {
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            socket.on_connect_func([&socket]() {
                std::cout<<"socket connected"<<std::endl;
                socket.write("Hello!",6);
            });
            socket.on_close_func([]() {
                std::cout<<"socket disconnected"<<std::endl;
            });
            socket.on_error_func([](error_c& err){
                std::cout<<"Error:"<<err.place()<<" "<<err.message()<<std::endl;
            });
        }
    });
    loop.run();
    return 0;
}*/

int test_if_address() {
    int fd;
    struct ifreq ifr;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;

    /* I want IP address attached to "eth0" */
    strncpy(ifr.ifr_name, "wlp16s0", IFNAMSIZ-1);

    int ret = ioctl(fd, SIOCGIFADDR, &ifr);
    if (ret != -1) {
        /* display result */
        printf("%s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
    } else {
        std::cout<<errno_c().message()<<std::endl;
    }
    close(fd);
    return 0;
}

#include <linux/if_link.h>
int getifaddr_test() {
   struct ifaddrs *ifaddr;
   int family, s;
   char host[NI_MAXHOST];

   if (getifaddrs(&ifaddr) == -1) {
       perror("getifaddrs");
       exit(EXIT_FAILURE);
   }

   /* Walk through linked list, maintaining head pointer so we
      can free list later */

   for (struct ifaddrs *ifa = ifaddr; ifa != NULL;
            ifa = ifa->ifa_next) {
       if (ifa->ifa_addr == NULL)
           continue;

       family = ifa->ifa_addr->sa_family;

       /* Display interface name and family (including symbolic
          form of the latter for the common families) */

       printf("%-8s %s (%d)\n",
              ifa->ifa_name,
              (family == AF_PACKET) ? "AF_PACKET" :
              (family == AF_INET) ? "AF_INET" :
              (family == AF_INET6) ? "AF_INET6" : "???",
              family);

       /* For an AF_INET* interface address, display the address */

       if (family == AF_INET || family == AF_INET6) {
           s = getnameinfo(ifa->ifa_addr,
                   (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                         sizeof(struct sockaddr_in6),
                   host, NI_MAXHOST,
                   NULL, 0, NI_NUMERICHOST);
           if (s != 0) {
               printf("getnameinfo() failed: %s\n", gai_strerror(s));
               exit(EXIT_FAILURE);
           }

           printf("\t\taddress: <%s>\n", host);

       } else if (family == AF_PACKET && ifa->ifa_data != NULL) {
           struct rtnl_link_stats *stats = (rtnl_link_stats *)ifa->ifa_data;

           printf("\t\ttx_packets = %10u; rx_packets = %10u\n"
                  "\t\ttx_bytes   = %10u; rx_bytes   = %10u\n",
                  stats->tx_packets, stats->rx_packets,
                  stats->tx_bytes, stats->rx_bytes);
       }
   }

   freeifaddrs(ifaddr);
   exit(EXIT_SUCCESS);
}

int main() {
    log::init();
    log::set_level(log::Level::DEBUG);
    //return addrinfo_test();
    //getifaddr_test();
    //return timer_test();
    //return udp_client_base_test();
    //return test_if_address();
    //return udp_client_test();
    return tcp_client_test();
    //return udp_server_test();
    //return udp_server_base_test();
    //return test_tcp_client_base();
}
