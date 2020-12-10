#ifndef __EPOLL_H__
#define __EPOLL_H__
#include <stdint.h>
#include <memory>
#include <functional>
#include <string>
#include "err.h"


class IOLoop;

class IOWriteable {
public:
    virtual ~IOWriteable() {}
    virtual int write(const void* buf, int len) = 0;
};

class IOPollable {
public:
    enum {
        NOT_HANDLED = 0,
        HANDLED,
        STOP
    };
    IOPollable(const std::string& n):name(n) {}
    virtual ~IOPollable() {}
    virtual bool epollEvent(int /*event*/) { return false; }
    virtual int epollIN() { return NOT_HANDLED; }
    virtual int epollOUT() { return NOT_HANDLED; }
    virtual int epollPRI() { return NOT_HANDLED; }
    virtual int epollERR() { return NOT_HANDLED; }
    virtual int epollRDHUP() { return NOT_HANDLED; }
    virtual int epollHUP() { return NOT_HANDLED; }
    virtual error_c start_with(IOLoop* loop) {return error_c(ENOTSUP);}
    virtual void cleanup() {}

    std::string name;
};

class IOLoop {
public:
    IOLoop(int size=8);
    ~IOLoop();
    error_c execute(IOPollable* obj);
    errno_c add(int fd, uint32_t events, IOPollable* obj);
    errno_c mod(int fd, uint32_t events, IOPollable* obj);
    errno_c del(int fd, IOPollable* obj);
    int run();
    void stop();
private:
    class IOLoopImpl;
    std::unique_ptr<IOLoopImpl> _impl;
};

using OnReadFunc  = std::function<void(void*, int)>;
using OnEventFunc = std::function<void()>;

#endif //__EPOLL_H__