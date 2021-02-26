#include <iostream>
#include <cstring>

#include <log.h>
#include <err.h>
#include <loop.h>
#include <udp.h>

int udp_client_test() {
    IOLoop loop;
    auto udp = UdpClient::create("MyEndpoint", &loop);
    udp->init("localhost",20001);
    udp->on_read([](void* buf, int len){
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    udp->on_connect([&udp]() {
        std::cout<<"on connect "<<udp->write("Hello!", 6)<<std::endl;
    });
    udp->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec<<std::endl;
    });
    loop.run();
    return 0;
}

int udp_server_test() {
    /*IOLoop loop;
    auto udp = UdpServer::create("MyEndpoint");
    udp->init(20001, &loop);
    udp->on_read([&udp](void* buf, int len){
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
        const char* answ = "Hello from server!";
        udp->write(answ, strlen(answ));
    });
    udp->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    loop.run();*/
    return 0;
}

int udp_test() {
    IOLoop loop;

    auto server = UdpServer::create("ServerEndpoint", &loop);
    server->init(20001);
    std::shared_ptr<UdpStream> client_stream;
    server->on_connect([&client_stream](std::shared_ptr<UdpStream> stream){
        client_stream = stream;
        stream->on_read([&client_stream](void* buf, int len) {
            std::cout<<"Server reads: ";
            std::cout.write((char*)buf,len);
            std::cout<<std::endl;
            const char* answ = "Hello from server!";
            client_stream->write(answ, strlen(answ));
        });
    });
    server->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec<<std::endl;
    });

    auto client = UdpClient::create("ClientEndpoint", &loop);
    client->init("localhost",20001);
    client->on_read([](void* buf, int len){
        std::cout<<"Client reads: ";
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    client->on_connect([&client]() {
        std::cout<<"on connect "<<client->write("Hello!", 6)<<std::endl;
    });
    client->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec<<std::endl;
    });

    loop.run();
    return 0;
}

//sudo firewall-cmd --permanent --direct --add-rule ipv4 filter INPUT 0 -m pkttype --pkt-type broadcast -j ACCEPT
//sudo firewall-cmd --permanent --direct --add-rule ipv4 filter INPUT 0 -m pkttype --pkt-type multicast -j ACCEPT
//sudo firewall-cmd --reload
int udp_broadcast_test() {
    /*IOLoop loop;

    auto server = UdpServer::create("ServerEndpoint");
    server->on_read([&server](void* buf, int len){
        std::cout<<"Server reads: ";
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
        const char* answ = "Hello from server!";
        server->write(answ, strlen(answ));
    });
    server->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    server->init_broadcast(15000, &loop, "<broadcast>");

    auto client = UdpClient::create("ClientEndpoint");
    client->on_read([](void* buf, int len){
        std::cout<<"Client reads: ";
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    client->on_connect([&client]() {
        std::cout<<"on connect "<<client->write("Hello!", 6)<<std::endl;
    });
    client->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    client->init_broadcast(15000, &loop,"<broadcast>");
    loop.run();*/
    return 0;
}

int udp_multicast_test() {
    /*IOLoop loop;

    auto server = UdpServer::create("ServerEndpoint");
    server->on_read([&server](void* buf, int len){
        std::cout<<"Server reads: ";
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
        const char* answ = "Hello mcast from server!";
        server->write(answ, strlen(answ));
    });
    server->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    server->init_multicast("239.0.0.1",6000, &loop);

    auto client = UdpClient::create("ClientEndpoint");
    client->on_read([](void* buf, int len){
        std::cout<<"Client reads: ";
        std::cout.write((char*)buf,len);
        std::cout<<std::endl;
    });
    client->on_connect([&client]() {
        std::cout<<"on connect "<<client->write("Hello mcast!", 13)<<std::endl;
    });
    client->on_error([](const error_c& ec) {
        std::cout<<"Udp socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
    });
    client->init_multicast("239.0.0.1",6000, &loop);
    loop.run();*/
    return 0;
}

int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"udp"});
    return udp_test();
    //return udp_broadcast_test();
    //return udp_multicast_test();
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
