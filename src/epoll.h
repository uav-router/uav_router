#ifndef __EPOLL_H__
#define __EPOLL_H__
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include "err.h"


class IOLoop;

class IOWriteable {
public:
    virtual ~IOWriteable() = default;
    virtual auto write(const void* buf, int len) -> int = 0;
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
    virtual void udev_add(const std::string& node, const std::string& id) {};
    virtual void udev_remove(const std::string& node, const std::string& id) {};
    virtual auto start_with(IOLoop* loop) -> error_c {return error_c(ENOTSUP);}
    virtual void cleanup() {}

    std::string name;
};

class IOLoop {
public:
    IOLoop(int size=8);
    ~IOLoop();
    auto execute(IOPollable* obj) -> error_c;
    auto add(int fd, uint32_t events, IOPollable* obj) -> errno_c;
    auto mod(int fd, uint32_t events, IOPollable* obj) -> errno_c;
    auto del(int fd, IOPollable* obj) -> errno_c;
    void udev_start_watch(IOPollable* obj);
    void udev_stop_watch(IOPollable* obj);
    auto udev_find_id(const std::string& path) -> std::string;
    auto udev_find_path(const std::string& id) -> std::string;
    auto run() -> int;
    void stop();
private:
    class IOLoopImpl;
    std::unique_ptr<IOLoopImpl> _impl;
};

using OnReadFunc  = std::function<void(void*, int)>;
using OnEventFunc = std::function<void()>;

#endif //__EPOLL_H__