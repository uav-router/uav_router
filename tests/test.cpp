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

/*int addrinfo_test() {
    AddrInfo request;
    //request.init("localhost","2020",AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    request.init("localhost","2020",0,0,0,AI_PASSIVE | AI_CANONNAME);
    IOLoop loop;
    loop.execute(&request);
    request.on_result_func(addr_info_result);
    loop.run();
    return 0;
}*/

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
#include <unordered_set>

int tcp_server_test() {
    IOLoop loop;
    std::unique_ptr<TcpServer> tcp = TcpServer::create("MyEndpoint");
    std::unordered_set<std::unique_ptr<TcpSocket>> sockets;
    tcp->init(10000,&loop);
    tcp->on_connect([&sockets](std::unique_ptr<TcpSocket>& socket, sockaddr* addr, socklen_t len){
        print_addr(addr,len);
        auto ret = sockets.insert(std::move(socket));
        if (std::get<1>(ret)) {
            auto sock = std::get<0>(ret);
            (*sock)->write("Hello!",6);
            (*sock)->on_read([](void* buf, int len){
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            (*sock)->on_close([sock, &sockets](){
                std::cout<<"Tcp socket closed "<<sockets.size()<<std::endl;
                sockets.erase(sock);
                std::cout<<"Tcp socket closed end "<<sockets.size()<<std::endl;
            });
            (*sock)->on_error([](const error_c& ec) {
                std::cout<<"Tcp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
            });
        } else {
            std::cout<<"Error: socket can not inserted to set"<<std::endl;
        }
    });
    loop.run();
    return 0;
}

/*int tcp_server_base_test() {
    IOLoop loop;
    TcpServerBase tcp;
    std::unordered_set<std::unique_ptr<TcpSocket>> sockets;
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family    = AF_INET; // IPv4 
    servaddr.sin_addr.s_addr = INADDR_ANY; //"192.168.0.25",10000
    servaddr.sin_port = htons(10000);
    tcp.init(sizeof(servaddr), (sockaddr*)&servaddr);
    tcp.on_connect([&sockets](std::unique_ptr<TcpSocket>& socket, sockaddr* addr, socklen_t len){
        print_addr(addr,len);
        auto ret = sockets.insert(std::move(socket));
        if (std::get<1>(ret)) {
            auto sock = std::get<0>(ret);
            (*sock)->write("Hello!",6);
            (*sock)->on_read([](void* buf, int len){
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            (*sock)->on_close([sock, &sockets](){
                std::cout<<"Tcp socket closed "<<sockets.size()<<std::endl;
                sockets.erase(sock);
                std::cout<<"Tcp socket closed end "<<sockets.size()<<std::endl;
            });
            (*sock)->on_error([](const error_c& ec) {
                std::cout<<"Tcp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
            });
        } else {
            std::cout<<"Error: socket can not inserted to set"<<std::endl;
        }
    });
    loop.execute(&tcp);
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
    udp.on_error([](const error_c& ec) {
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
    udp.on_error([&udp](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    loop.run();
    return 0;
}


int tcp_client_test() {
    IOLoop loop;
    TcpClient tcp("MyEndpoint");
    tcp.init("192.168.0.25",10000,&loop);
    tcp.on_error([&tcp](const error_c& ec) {
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
            socket.on_error([](error_c& err){
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
    //return tcp_client_test();
    //return udp_server_test();
    //return udp_server_base_test();
    //return test_tcp_client_base();
    //return tcp_server_base_test();
    return tcp_server_test();
}
