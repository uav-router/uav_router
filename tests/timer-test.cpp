#include <memory>
#include <iostream>
#include <chrono>
using namespace std::chrono_literals;
#include <log.h>
#include <loop.h>
#include <timer.h>

void test() {
    IOLoop loop;
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
    log::init();
    log::set_level(log::Level::DEBUG);
    test();
    return 0;
}