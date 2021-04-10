#ifndef __IOLOOP_H__
#define __IOLOOP_H__
#include <memory>
#include <string>
#include "err.h"
#include "measure.h"

#include "inc/endpoints.h"
#include "inc/timer.h"
#include "inc/signal.h"

// ---------------------------------
class IOLoop  : public error_handler {
public:
    // loop items
    virtual auto uart(const std::string& name) -> std::unique_ptr<UART> = 0;
    //virtual auto service_client(const std::string& name) -> std::unique_ptr<ServiceClient> = 0;
    virtual auto tcp_client(const std::string& name) -> std::unique_ptr<TcpClient> = 0;
    //virtual auto udp_client(const std::string& name) -> std::unique_ptr<UdpClient> = 0;
    virtual auto tcp_server(const std::string& name) -> std::unique_ptr<TcpServer> = 0;
    //virtual auto udp_server(const std::string& name) -> std::unique_ptr<UdpServer> = 0;
    virtual auto signal_handler() -> std::unique_ptr<Signal> = 0;
    virtual auto timer() -> std::unique_ptr<Timer> = 0;
    
    virtual void block_udev() = 0;
    virtual void block_zeroconf() = 0;
    
    // stats
    //virtual auto stats() -> StatHandler& = 0;

    // run
    virtual void run()  = 0;
    virtual void stop() = 0;

    virtual auto handle_CtrlC() -> error_c = 0;
    static auto loop(int pool_events=5) -> std::unique_ptr<IOLoop>;
};


#endif //__IOLOOP_H__

