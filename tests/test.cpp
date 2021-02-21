#include <unistd.h>
#include <ifaddrs.h>

#include <sys/ioctl.h>

#include <net/if.h>
#include <arpa/inet.h>

#include <iostream>
#include <iomanip>

#include <chrono>
using namespace std::chrono_literals;

#include <cstring>

#include <log.h>
#include <err.h>
#include <loop.h>
#include <addrinfo.h>
#include <timer.h>
#include <uart.h>

//g++ test.cpp src/udp.cpp src/timer.cpp src/err.cpp src/epoll.cpp src/log.cpp src/addrinfo.cpp -I src -o test -lanl

void print_addr(const struct sockaddr *sa, socklen_t salen) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    eai_code ec = getnameinfo(sa, salen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ec) {
       Log::error()<<"Get nameinfo error: "<<ec.message()<<" family="<<sa->sa_family<<" len="<<salen<<std::endl;
    } else {
        Log::info()<<"address = "<<host<<":"<<port<<std::endl;
    }
}

void addr_info_result(addrinfo* addr, const std::error_code& res) {
    if (res) {
        Log::error()<<"Get address error "<<": "<<res.message()<<std::endl;
    } else {
        char host[NI_MAXHOST];
        char port[NI_MAXSERV];
        while(addr) {
            eai_code ec = getnameinfo(addr->ai_addr, addr->ai_addrlen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
            if (ec) {
               Log::error()<<"Get nameinfo error: "<<ec.message()<<std::endl;
            } else {
                Log::info()<<"address = "<<host<<":"<<port
                <<" family "<<addr->ai_family<<" socktype "<<addr->ai_socktype
                <<" protocol "<<addr->ai_protocol<<" flags "<<addr->ai_flags;
                if (addr->ai_canonname) Log::info()<<" canonname "<<addr->ai_canonname;
                Log::info()<<std::endl;
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
    IOLoop loop;
    request.start_with(&loop);
    request.arm_periodic(2s);
    request.on_shoot_func([] () {std::cout<<"Timer event"<<std::endl;});
    loop.run();
    return 0;
}

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
           if (ifa->ifa_flags | IFF_BROADCAST) {
               s = getnameinfo(ifa->ifa_broadaddr,
                       (family == AF_INET) ? sizeof(struct sockaddr_in) :
                                             sizeof(struct sockaddr_in6),
                       host, NI_MAXHOST,
                       NULL, 0, NI_NUMERICHOST);
               if (s != 0) {
                   printf("getnameinfo() failed: %s\n", gai_strerror(s));
                   exit(EXIT_FAILURE);
               }
               printf("\t\tbroadcast address: <%s>\n", host);
           }

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

int uart_test() {
    IOLoop loop;
    auto uart = UART::create("UartEndpoint");
    uart->init("/dev/ttyUSB0",&loop,115200);//921600);
    uart->on_connect([]() {
        std::cout<<"uart connected"<<std::endl;
    });
    uart->on_close([]() {
        std::cout<<"uart disconnected"<<std::endl;
    });
    uart->on_read([](void* buf, int len) {
        std::cout<<"UART("<<len<<"): ";
        const char* hex = "0123456789ABCDEF";
        uint8_t *arr = (uint8_t*)buf;
        for (int i = len; i; i--) {
            uint8_t b = *arr++;
            std::cout<<hex[(b>>4) & 0x0F];
            std::cout<<hex[b & 0x0F]<<' ';
        }
            
        std::cout<<std::endl;
    });
    uart->on_error([](const error_c& ec) {
        std::cout<<"UART error:"<<ec<<std::endl;
    });
    loop.run();
    return 0;
}

int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG);
    //return addrinfo_test();
    //getifaddr_test();
    //return timer_test();
    //return test_if_address();
    //return uart_test();
    return getifaddr_test();
}
