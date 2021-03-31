#include <chrono>
#include <string>
using namespace std::chrono_literals;

#include "log.h"
#include "ioloop.h"

void test() {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto cli = loop->tcp_client("TcpClient");
    {
        std::shared_ptr<Client> endpoint;
        cli->on_connect([&endpoint](std::shared_ptr<Client> cli, std::string name){
            endpoint = cli;
            endpoint->on_close([name, &endpoint](){
                std::cout<<"Close "<<name<<std::endl;
                //endpoint.reset();
            });
            endpoint->on_read([name](void* buf, int len){
                std::cout<<"Client "<<name<<" ("<<len<<"): ";
                std::cout.write((char*)buf,len);
                std::cout<<std::endl;
            });
            endpoint->on_error([](const error_c& ec) {
                std::cout<<"UART cli error:"<<ec<<std::endl;
            });
            std::string msg = "Hello, server!";
            endpoint->write(msg.data(),msg.size());
            std::cout<<"Connect to "<<name<<std::endl;
        });
        cli->on_error([](const error_c& ec) {
            std::cout<<"TcpClient error:"<<ec<<std::endl;
        });
        cli->init("192.168.0.25",10000);
    }
    loop->run();
}


int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop","tcpclient"});
    test();
    return 0;
}