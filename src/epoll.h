#ifndef __EPOLL_H__
#define __EPOLL_H__
#include <stdint.h>
#include <memory>
#include <functional>
#include "err.h"


class IOLoop;

class IOWriteable {
public:
    virtual int write(const void* buf, int len) = 0;
};

class IOPollable :  public error_handler {
public:
    virtual ~IOPollable() {}
    virtual void events(IOLoop* loop, uint32_t evs) = 0;
    virtual error_c start_with(IOLoop* loop) {return error_c(0);}
    virtual void cleanup() {}
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