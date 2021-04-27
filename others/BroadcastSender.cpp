#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket() and bind() */
#include <arpa/inet.h>  /* for sockaddr_in */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <iostream>
#include <netdb.h>

#define MAXRECVSTRING 255  /* Longest string to receive */
void DieWithError(const char *errorMessage) {
    printf("%s\n",errorMessage);
}

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


int main(int argc, char *argv[])
{
    int sock;                         /* Socket */
    struct sockaddr_in broadcastAddr; /* Broadcast address */
    char *broadcastIP;                /* IP broadcast address */
    unsigned short broadcastPort;     /* Server port */
    char *sendString;                 /* String to broadcast */
    int broadcastPermission;          /* Socket opt to set permission to broadcast */
    unsigned int sendStringLen;       /* Length of string to broadcast */

    if (argc < 4)                     /* Test for correct number of parameters */
    {
        fprintf(stderr,"Usage:  %s <IP Address> <Port> <Send String>\n", argv[0]);
        exit(1);
    }

    broadcastIP = argv[1];            /* First arg:  broadcast IP address */ 
    broadcastPort = atoi(argv[2]);    /* Second arg:  broadcast port */
    sendString = argv[3];             /* Third arg:  string to broadcast */

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");

    /* Set socket to allow broadcast */
    broadcastPermission = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *) &broadcastPermission, 
          sizeof(broadcastPermission)) < 0)
        DieWithError("setsockopt(broadcast) failed");

    struct timeval tv = {.tv_sec=2, .tv_usec=0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval)) < 0) {
        perror("SO_RCVTIMEO");
    }

    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));   /* Zero out structure */
    bindAddr.sin_family = AF_INET;                 /* Internet address family */
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr("192.168.0.255"); //inet_addr("192.168.0.131");inet_addr("172.17.0.1")  /* Any incoming interface */

    /* Bind to the broadcast port */
    if (bind(sock, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) < 0) {
        perror("bind");
    } else {
        struct sockaddr_in actualAddr;
        socklen_t actualLen = sizeof(actualAddr);
        int ret = getsockname(sock,(sockaddr*)&actualAddr, &actualLen);
        if (ret) {
            perror("getsockname");
        } else {
            print_addr((sockaddr *) &actualAddr, actualLen, "Actual");
        }
    }

    /* Construct local address structure */
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));   /* Zero out structure */
    broadcastAddr.sin_family = AF_INET;                 /* Internet address family */
    broadcastAddr.sin_addr.s_addr = inet_addr(broadcastIP);/* Broadcast IP address */
    broadcastAddr.sin_port = htons(broadcastPort);         /* Broadcast port */
    char buf[1024];
    int cnt = 0;
    for (;;) /* Run forever */
    {
         /* Broadcast sendString in datagram to clients every 3 seconds*/
         sendStringLen = snprintf(buf,1024,"%s:%i", sendString, cnt++);  /* Find length of sendString */
         printf("Send: %s\n",buf);
         if (sendto(sock, buf, sendStringLen, 0, (struct sockaddr *) 
               &broadcastAddr, sizeof(broadcastAddr)) != sendStringLen)
             DieWithError("sendto() sent a different number of bytes than expected");
    
        char recvString[MAXRECVSTRING+1]; /* Buffer for received string */
        int recvStringLen;                /* Length of received string */
        sockaddr_storage recvaddr;
        socklen_t recvlen = sizeof(recvaddr);
        if ((recvStringLen = recvfrom(sock, recvString, MAXRECVSTRING, 0, (sockaddr*)&recvaddr, &recvlen)) < 0) { DieWithError("recvfrom() failed");
        } else {
            recvString[recvStringLen] = '\0';
            printf("Answer: %s \n", recvString);    /* Print the received string */
            print_addr((sockaddr *)&recvaddr, recvlen, "From");
        }
        sleep(1);   /* Avoids flooding the network */
    }
    /* NOT REACHED */
}