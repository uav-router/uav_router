#include "log.h"
#include "endpoint.h"

void test() {
    auto loop = IOLoop::loop();
    loop->handle_CtrlC();
    loop->run();
}

int main() {
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"ioloop"});
    test();
    return 0;
}