#include <chrono>
using namespace std::chrono_literals;
#include <iostream>

#include "log.h"
#include "loop.h"


void test() {
    auto loop = IOLoopSvc::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto resolver = loop->address();
    resolver->init("google.com", 80, [](SockAddrList&& list){
        auto l = std::move(list);
        std::cout<<"Addreses of google.com:"<<std::endl;
        for (auto& addr : l) {
            std::cout<<addr<<std::endl;
        }
        std::cout<<"=================="<<std::endl;
    });
    loop->run();
}


int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop"});
    test();
    return 0;
}