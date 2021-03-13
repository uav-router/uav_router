#ifndef __POLL_H__
#define __POLL_H__

#include "../err.h"

class IOPollable;
class Poll {
public:
    virtual auto add(int fd, uint32_t events, IOPollable* obj) -> errno_c = 0;
    virtual auto mod(int fd, uint32_t events, IOPollable* obj) -> errno_c = 0;
    virtual auto del(int fd, IOPollable* obj) -> errno_c = 0;
};

class IOPollable {
public:
    enum {
        NOT_HANDLED = 0,
        HANDLED,
        STOP
    };
    IOPollable(std::string  n):name(std::move(n)) {}
    virtual ~IOPollable() = default;
    virtual auto epollEvent(int /*event*/) -> bool { return false; }
    virtual auto epollIN() -> int { return NOT_HANDLED; }
    virtual auto epollOUT() -> int { return NOT_HANDLED; }
    virtual auto epollPRI() -> int { return NOT_HANDLED; }
    virtual auto epollERR() -> int { return NOT_HANDLED; }
    virtual auto epollRDHUP() -> int { return NOT_HANDLED; }
    virtual auto epollHUP() -> int { return NOT_HANDLED; }
    virtual void cleanup() {}
    std::string name;
};

#endif //__POLL_H__
