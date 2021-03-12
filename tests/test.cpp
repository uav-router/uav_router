#include "endpoint.h"

void test() {
    auto loop = IOLoop::loop();
    loop->run();
}

int main() {
    test();
    return 0;
}