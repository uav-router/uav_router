#include "endpoint.h"
#include <memory>

class IOLoopImpl : public IOLoop {
    // loop items
    //auto uart(const std::string& name) -> std::unique_ptr<UART> override {}
    //auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> override {}
    //auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> override {}
    //auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> override {}
    //auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> override {}
    //auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> override {}
    // stats
    //auto stats() -> StatHandler& override {}
    // run
    auto run() -> int override {}
    void stop() override {}
    //auto handle_CtrlC() -> error_c override {}

    //auto handle_udev() -> error_c override {}
    //auto handle_zeroconf() -> error_c override {}
};

auto IOLoop::loop() -> std::unique_ptr<IOLoop> {
    return std::make_unique<IOLoopImpl>();
}

