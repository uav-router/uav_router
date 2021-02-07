#include <netinet/in.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>

int main() {
    sockaddr_in addr;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd==-1) {
        perror("socket");
        return 1;
    }
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr("192.168.4.119");
    addr.sin_port = 0;
    printf("addr=%i port=%i\n",int(addr.sin_addr.s_addr), int(addr.sin_port));
    int ret = bind(fd,(sockaddr*)&addr, sizeof(addr));
    if (ret==-1) {
        perror("bind");
        return 1;
    }
    sockaddr_in out;
    socklen_t len = sizeof(out);
    ret = getsockname(fd,(sockaddr*)&out,&len);
    if (ret==-1) {
        perror("getsockname");
        return 1;
    }
    printf("addr=%i port=%i\n",int(out.sin_addr.s_addr), int(out.sin_port));
    //printf("port=%i\n",out.sin_port);
}