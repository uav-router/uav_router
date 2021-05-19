#ifndef __LOOP__H__
#define __LOOP__H__
#include "ioloop.h"

#include "inc/poll.h"
#include "inc/udev.h"
#include "inc/zeroconf.h"
#include "inc/address.h"
#include "inc/stat.h"
#include <memory>

//-------------------------------------

class IOLoopSvc : public IOLoop {
public:
    virtual auto poll() -> Poll* = 0;
    virtual auto udev() -> UdevLoop* = 0;
    virtual auto zeroconf() -> Avahi* = 0;
    virtual auto address() -> std::unique_ptr<AddressResolver> = 0;
    //virtual void register_report(std::shared_ptr<Stat> source, std::chrono::nanoseconds period) = 0;
    static auto loop(int pool_events=5) -> std::unique_ptr<IOLoopSvc>;
};

#endif  //!__LOOP__H__