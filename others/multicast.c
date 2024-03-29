/* 

multicast.c

The following program sends or receives multicast packets. If invoked
with one argument, it sends a packet containing the current time to an
arbitrarily chosen multicast group and UDP port. If invoked with no
arguments, it receives and prints these packets. Start it as a sender on
just one host and as a receiver on all the other hosts

*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>

#define EXAMPLE_PORT 6000
#define EXAMPLE_GROUP "239.0.0.1"

main(int argc, char **)
{
    struct sockaddr_in addr;
    int sock, cnt;
    socklen_t addrlen;
    struct ip_mreq mreq;
    char message[50];
    //auto interface_address = inet_addr("192.168.4.118");
    auto interface_address = htonl(INADDR_ANY);

    /* set up socket */
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        perror("socket");
        exit(1);
    }
    bzero((char *)&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(EXAMPLE_PORT);
    addrlen = sizeof(addr);

    if (argc > 1)
    {
        /* send */
        unsigned char mc_ttl = 1;
        if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (void *)&mc_ttl, sizeof(mc_ttl)) < 0)
        {
            perror("mcast ttl");
            return 1;
        }
        if (interface_address!=htonl(INADDR_ANY)) {
            sockaddr_in interface;
            bzero((char *)&interface, sizeof(interface));
            interface.sin_addr.s_addr = interface_address;
            if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (void *)&interface.sin_addr, sizeof(interface.sin_addr)) < 0)
            {
                perror("mcast if");
                return 1;
            }
        }
        addr.sin_addr.s_addr = inet_addr(EXAMPLE_GROUP);
        while (1)
        {
            time_t t = time(0);
            sprintf(message, "time is %-24.24s", ctime(&t));
            printf("sending: %s\n", message);
            cnt = sendto(sock, message, sizeof(message), 0,
                         (struct sockaddr *)&addr, addrlen);
            if (cnt < 0)
            {
                perror("sendto");
                exit(1);
            }
            sleep(5);
        }
    }
    else
    {
        int yes = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) < 0)
        {
            perror("reuse port");
            if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
            {
                perror("reuse addr");
                return 1;
            }
        }
        /* receive */
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            perror("bind");
            exit(1);
        }
        mreq.imr_multiaddr.s_addr = inet_addr(EXAMPLE_GROUP);
        mreq.imr_interface.s_addr = interface_address;
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                       &mreq, sizeof(mreq)) < 0)
        {
            perror("setsockopt mreq");
            exit(1);
        }
        while (1)
        {
            cnt = recvfrom(sock, message, sizeof(message), 0,
                           (struct sockaddr *)&addr, &addrlen);
            if (cnt < 0)
            {
                perror("recvfrom");
                exit(1);
            }
            else if (cnt == 0)
            {
                break;
            }
            printf("%s: message = \"%s\"\n", inet_ntoa(addr.sin_addr), message);
        }
    }
}
