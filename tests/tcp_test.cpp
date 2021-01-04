
#include <iostream>
#include <unordered_set>

#include <log.h>
#include <err.h>
#include <epoll.h>
#include <tcp.h>
#include <timer.h>
#include <utility>
using namespace std::chrono_literals;


void print_addr(const struct sockaddr *sa, socklen_t salen) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    eai_code ec = getnameinfo(sa, salen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ec) {
       std::cout<<"Get nameinfo error: "<<ec.message()<<" family="<<sa->sa_family<<" len="<<salen<<std::endl;
    } else {
        std::cout<<"address = "<<host<<":"<<port<<std::endl;
    }
}

int tcp_client_test() {
    IOLoop loop;
    auto tcp = TcpClient::create("MyEndpoint");
    tcp->init("192.168.0.25",10000,&loop);
    tcp->on_error([&tcp](const error_c& ec) {
        std::cout<<"Tcp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    tcp->on_read([](void* buf, int len) {
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    tcp->on_connect([&tcp]() {
        std::cout<<"socket connected"<<std::endl;
        tcp->write("Hello!",6);
    });
    tcp->on_close([]() {
        std::cout<<"socket disconnected"<<std::endl;
    });
    loop.run();
    return 0;
}

int tcp_test() {
    IOLoop loop;
    
    std::unique_ptr<TcpClient> client;
    Timer timer;
    int tryno = 0;
    timer.on_shoot_func([&client,&tryno, &loop](){
        client = TcpClient::create("ClientEndpoint");
        client->init("192.168.0.25",10000,&loop);
        client->on_error([](const error_c& ec) {
            std::cout<<"Tcp client socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
        });
        client->on_read([](void* buf, int len) {
            std::cout<<"Tcp client: ";
            std::cout.write((char*)buf,len);
            std::cout<<std::endl;
        });
        client->on_connect([&client,&tryno]() {
            std::cout<<"client socket connected"<<std::endl;
            char buf[256];
            int size = snprintf(buf,sizeof(buf),"Hello %i!",tryno++);
            client->write(buf,size);
            std::cout<<"client connect func end"<<std::endl;
        });
        client->on_close([]() {
            std::cout<<"socket disconnected"<<std::endl;
        });
    });
    timer.init_periodic(3s);
    timer.start_with(&loop);

    
    std::unordered_set<std::unique_ptr<TcpSocket>> sockets;
    auto server = TcpServer::create("ServerEndpoint");
    server->init(10000,&loop);
    server->on_connect([&sockets](std::unique_ptr<TcpSocket>& socket, sockaddr* addr, socklen_t len){
        print_addr(addr,len);
        auto ret = sockets.insert(std::move(socket));
        if (std::get<1>(ret)) {
            auto sock = std::get<0>(ret);
            (*sock)->write("Hello!",6);
            (*sock)->on_read([](void* buf, int len){
                std::cout<<"Tcp server: ";
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            (*sock)->on_close([sock, &sockets](){
                std::cout<<"Tcp socket closed "<<sockets.size()<<std::endl;
                sockets.erase(sock);
                std::cout<<"Tcp socket closed end "<<sockets.size()<<std::endl;
            });
            (*sock)->on_error([](const error_c& ec) {
                std::cout<<"Tcp server socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
            });
        } else {
            std::cout<<"Error: socket can not inserted to set"<<std::endl;
        }
    });
    server->on_close([](){
        std::cout<<"Server closed"<<std::endl;
    });
    server->on_error([](const error_c& ec) {
        std::cout<<"Tcp server error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });

    loop.run();
    return 0;
}

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

int main() {
    log::init();
    log::set_level(log::Level::DEBUG);

    return tcp_test();
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
