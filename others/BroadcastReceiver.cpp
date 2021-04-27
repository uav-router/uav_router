#include <netinet/in.h>
#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), sendto(), and recvfrom() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <iostream>
#include <netdb.h>
#define MAXRECVSTRING 255  /* Longest string to receive */


void print_addr(const struct sockaddr *sa, socklen_t salen, const char* prefix = nullptr) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    int ec = getnameinfo(sa, salen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ec) {
       std::cout<<"Get nameinfo error: "<<ec<<" family="<<sa->sa_family<<" len="<<salen<<std::endl;
    } else {
        if (prefix) std::cout<<prefix;
        std::cout<<" address = "<<host<<":"<<port<<std::endl;
    }
}

void DieWithError(const char *errorMessage){
    printf(errorMessage);
}

int main(int argc, char *argv[])
{
    int sock;                         /* Socket */
    struct sockaddr_in broadcastAddr; /* Broadcast Address */
    unsigned short broadcastPort;     /* Port */
    char recvString[MAXRECVSTRING+1]; /* Buffer for received string */
    int recvStringLen;                /* Length of received string */

    if (argc != 2)    /* Test for correct number of arguments */
    {
        fprintf(stderr,"Usage: %s <Broadcast Port>\n", argv[0]);
        exit(1);
    }

    broadcastPort = atoi(argv[1]);   /* First arg: broadcast port */

    /* Create a best-effort datagram socket using UDP */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");

    int broadcastPermission = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, 
          sizeof(broadcastPermission)) < 0)
        DieWithError("setsockopt() failed");

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void *) &broadcastPermission, 
          sizeof(broadcastPermission)) < 0)
        DieWithError("setsockopt() failed");
    /* Construct bind structure */
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));   /* Zero out structure */
    broadcastAddr.sin_family = AF_INET;                 /* Internet address family */
    broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);//inet_addr("192.168.0.255"); //inet_addr("192.168.0.131");inet_addr("172.17.0.1")  /* Any incoming interface */
    broadcastAddr.sin_port = htons(broadcastPort);      /* Broadcast port */

    /* Bind to the broadcast port */
    if (bind(sock, (struct sockaddr *) &broadcastAddr, sizeof(broadcastAddr)) < 0)
        DieWithError("bind() failed");

    struct sockaddr_in actualAddr;
    socklen_t actualLen = sizeof(actualAddr);
    int ret = getsockname(sock,(sockaddr*)&actualAddr, &actualLen);
    if (ret) {
        perror("getsockname");
    } else {
        print_addr((sockaddr *) &actualAddr, actualLen, "Actual");
    }
    for(;;) {
    /* Receive a single datagram from the server */
    sockaddr_storage client_addr;
    socklen_t ca_len = sizeof(client_addr);
    if ((recvStringLen = recvfrom(sock, recvString, MAXRECVSTRING, 0, (sockaddr *) &client_addr, &ca_len)) < 0) {
        DieWithError("recvfrom() failed");
    } else {
        recvString[recvStringLen] = '\0';
        printf("Received: %s ", recvString);    /* Print the received string */
        print_addr((sockaddr *) &client_addr, ca_len);
        if (sendto(sock, recvString, recvStringLen, 0, (sockaddr *) &client_addr, ca_len) != recvStringLen)
                 DieWithError("sendto() sent a different number of bytes than expected");
        }
    }
    close(sock);
    exit(0);
}
