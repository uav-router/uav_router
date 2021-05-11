#ifndef __TIMER_INC_H__
#define __TIMER_INC_H__
#include <chrono>
#include "endpoints.h"

class Timer : public error_handler {
public:
    virtual auto arm_periodic(std::chrono::nanoseconds timeout) -> error_c = 0;
    virtual auto arm_oneshoot(std::chrono::nanoseconds timeout) -> error_c = 0;
    virtual auto arm(const itimerspec * value, int flags) -> error_c = 0;
    virtual auto clockid(int id) -> Timer& = 0;
    virtual auto shoot(OnEventFunc func) -> Timer& = 0;
    virtual void stop() = 0;
    virtual auto armed() -> bool = 0;
};

#endif  //!__TIMER_INC_H__