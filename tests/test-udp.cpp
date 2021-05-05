#include <chrono>
#include <string>
using namespace std::chrono_literals;

#include "log.h"
#include "ioloop.h"

void test(const std::string& addr, int port, UdpServer::Mode mode = UdpServer::UNICAST) {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto cli = loop->udp_client("UdpClient");        
    std::shared_ptr<Client> endpoint;
    cli->on_connect([&endpoint, &cli, &loop](std::shared_ptr<Client> stream, std::string name){
        endpoint = stream;
        endpoint->on_close([name, &endpoint,&cli,&loop](){
            std::cout<<"Close "<<name<<std::endl;
            endpoint.reset();
            cli.reset();
            loop->stop();
        });
        endpoint->on_read([name,&endpoint,&cli,&loop](void* buf, int len){
            std::cout<<"Client "<<name<<" ("<<len<<"): ";
            std::cout.write((char*)buf,len);
            std::cout<<std::endl;
            endpoint.reset();
            cli.reset();
            loop->stop();
        });
        endpoint->on_error([](const error_c& ec) {
            std::cout<<"UDP cli error:"<<ec<<std::endl;
        });
        std::cout<<"Connect to "<<name<<std::endl;
    });
    cli->on_error([](const error_c& ec) {
        std::cout<<"UdpClient error:"<<ec<<std::endl;
        });
    cli->writeable([&cli]() {
        std::string msg = "Hello, server!";
        cli->write(msg.data(),msg.size());
        });
    loop->zeroconf_ready([&loop,&addr,port,&cli,mode](){
        std::cout<<"Client create"<<std::endl;
        if (port==0) {
            cli->init_service("UdpServer");
        } else {
            switch(mode) {
            case UdpServer::UNICAST: cli->init(addr,port); break;
            case UdpServer::BROADCAST: cli->init_broadcast(port); break;//TODO: specify interface
            case UdpServer::MULTICAST: cli->init_multicast(addr, port); break;//TODO: specify interface and ttl
            }
        }
    });
    loop->run();
}

void svr(int port, const std::string& addr="", UdpServer::Mode mode = UdpServer::UNICAST) {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto svr = loop->udp_server("UdpServer");
    std::shared_ptr<Client> endpoint;
    svr->on_connect([&endpoint](std::shared_ptr<Client> cli, std::string name){
        endpoint = cli;
        endpoint->on_close([name, &endpoint](){
            std::cout<<"Close "<<name<<std::endl;
            //endpoint.reset();
        });
        endpoint->on_read([name, &endpoint](void* buf, int len){
            std::cout<<"Server "<<name<<" read ("<<len<<"): ";
            std::cout.write((char*)buf,len);
            std::cout<<std::endl;
            std::string msg = "Hello, client!";
            int l = endpoint->write(msg.data(),msg.size());
            std::cout<<l<<" bytes written"<<std::endl;
        });
        endpoint->on_error([](const error_c& ec) {
            std::cout<<"UDP svr error:"<<ec<<std::endl;
        });
        std::cout<<"Accept from "<<name<<std::endl;
    });
    svr->on_error([](const error_c& ec) {
        std::cout<<"UdpServer error:"<<ec<<std::endl;
    });
    ec = svr->address(addr).init(port,mode);//TODO: specify interface, family, ttl
    if (ec) {
        std::cout<<"Error init connection: "<<ec<<std::endl;
        return;
    }
    loop->run();
}


int main(int argc, char** argv) {
    UdpServer::Mode mode = UdpServer::MULTICAST;
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop","udpclient","udpserver","svclistener","default"});
    std::string address = "192.168.0.25";
    int port = 10000;
    bool server = true;
    if (argc>1) {
        if (std::string(argv[1])=="server") {
            address = "";
            if (argc>2) {
                port = std::stoi(argv[2]);
                if (argc>3) {
                    address = argv[3];
                }
            }
            svr(port,address,mode);
        } else if (std::string(argv[1])=="client") {
            if (argc>2) {
                address = argv[2];
                if (argc>3) {
                    port = std::stoi(argv[3]);
                }
            }
            test(address,port,mode);
        }
    }
    return 0;
}