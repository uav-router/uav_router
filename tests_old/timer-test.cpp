#include <memory>
#include <iostream>
#include <chrono>
using namespace std::chrono_literals;
#include <log.h>
#include <loop.h>
#include <timer.h>

void test() {
    IOLoop loop;
    error_c ec = loop.handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto timer = std::make_unique<Timer>();
    timer->on_shoot_func([&timer](){
        std::cout<<"Shoot func"<<std::endl;
        timer->arm_oneshoot(1s);    
    });
    timer->start_with(&loop);
    timer->arm_oneshoot(1ns);
    loop.run();
}

int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG);
    test();
    return 0;
}