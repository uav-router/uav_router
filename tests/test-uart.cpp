#include "log.h"
#include "ioloop.h"
#include <memory>
#include <iostream>

void test() {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto uart = loop->uart("UartEndpoint");
    {
        std::shared_ptr<Client> endpoint;
        uart->on_connect([&endpoint](std::shared_ptr<Client> cli, std::string name){
            endpoint = cli;
            endpoint->on_close([name, &endpoint](){
                std::cout<<"Close "<<name<<std::endl;
                //endpoint.reset();
            });
            endpoint->on_read([name](void* buf, int len){
                std::cout<<"UART "<<name<<" ("<<len<<"): ";
                const char* hex = "0123456789ABCDEF";
                uint8_t *arr = (uint8_t*)buf;
                for (int i = len; i; i--) {
                    uint8_t b = *arr++;
                    std::cout<<hex[(b>>4) & 0x0F];
                    std::cout<<hex[b & 0x0F]<<' ';
                }
                std::cout<<std::endl;
            });
            endpoint->on_error([](const error_c& ec) {
                std::cout<<"UART cli error:"<<ec<<std::endl;
            });
            std::cout<<"Connect to "<<name<<std::endl;
        });
        uart->on_error([](const error_c& ec) {
            std::cout<<"UART error:"<<ec<<std::endl;
        });
        uart->init("/dev/ttyUSB0",115200);
        loop->run();
    }
}

int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop","uart"});
    test();
    return 0;
}