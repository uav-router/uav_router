#ifndef __LOOP__H__
#define __LOOP__H__
#include "ioloop.h"
class IOLoopSvc : public IOLoop {
public:
    virtual auto poll() -> Poll* = 0;
    virtual auto udev() -> UdevLoop* = 0;
};

#endif  //!__LOOP__H__