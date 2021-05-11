#include <chrono>
using namespace std::chrono_literals;

#include "log.h"
#include "ioloop.h"

void test_timer() {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto stats = loop->stats();
    stats->add_output(stats->influx_line("log.txt"));
    auto timer = loop->timer();
    ec = timer->shoot([&timer](){
        std::cout<<"Shoot func"<<std::endl;
        timer->arm_oneshoot(1s);
    }).arm_oneshoot(1ns);
    if (ec) {
        std::cout<<"Setup timer error "<<ec<<std::endl;
        return;
    }
    loop->run();
}

void test() {
    auto loop = IOLoop::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    loop->run();
}


int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop"});
    test_timer();
    return 0;
}