#ifndef __LOOP__H__
#define __LOOP__H__
#include "ioloop.h"

#include "inc/poll.h"
#include "inc/udev.h"
#include "inc/zeroconf.h"
//-------------------------------------
class IOLoopSvc : public IOLoop {
public:
    virtual auto poll() -> Poll* = 0;
    virtual auto udev() -> UdevLoop* = 0;
    virtual auto zeroconf() -> Avahi* = 0;
    static auto loop(int pool_events=5) -> std::unique_ptr<IOLoopSvc>;
};

#endif  //!__LOOP__H__