#ifndef __EPOLL_H__
#define __EPOLL_H__
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <chrono>
#include <avahi-common/watch.h>
#include "err.h"
#include "measure.h"
#include "avahi-cpp.h"

using OnReadFunc  = std::function<void(void*, int)>;
using OnEventFunc = std::function<void()>;

class IOLoop;

class IOWriteable {
public:
    virtual ~IOWriteable() = default;
    virtual auto write(const void* buf, int len) -> int = 0;
    void on_write_allowed(OnEventFunc func) {_write_allowed = func;}
protected:
    void write_allowed() {if (_write_allowed) _write_allowed(); }
private:
    OnEventFunc _write_allowed;
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

class IOLoop : public AvahiHandler {
public:
    IOLoop(int size=8);
    ~IOLoop() override;
    // poll
    auto add(int fd, uint32_t events, IOPollable* obj) -> errno_c;
    auto mod(int fd, uint32_t events, IOPollable* obj) -> errno_c;
    auto del(int fd, IOPollable* obj) -> errno_c;
    // udev
    void udev_start_watch(IOPollable* obj);
    void udev_stop_watch(IOPollable* obj);
    auto udev_find_id(const std::string& path) -> std::string;
    auto udev_find_path(const std::string& id) -> std::string;
    auto handle_udev() -> error_c;
    // stats
    void add_stat_output(std::unique_ptr<OStat> out);
    void clear_stat_outputs();
    void register_report(Stat* source, std::chrono::nanoseconds period);
    void unregister_report(Stat* source);
    // zeroconf
    auto query_service(CAvahiService pattern, AvahiLookupFlags flags=(AvahiLookupFlags)0) -> std::unique_ptr<AvahiQuery> override;
    auto get_register_group() -> std::unique_ptr<AvahiGroup> override;
    enum ServiceType {
        UDP,
        TCP
    };
    auto service_exists(const std::string& service_name) ->bool;
    auto get_service_info(const std::string& service_name, const std::string& interface, ServiceType type)->bool;
    auto handle_zeroconf() -> error_c;
    // run
    auto run() -> int;
    void stop();
    auto handle_CtrlC() -> error_c;
private:
    class IOLoopImpl;
    std::unique_ptr<IOLoopImpl> _impl;
};

extern auto influx_udp(const std::string& host, 
                uint16_t port, 
                IOLoop* loop,
                std::string global_tags = "",
                int pack_size = 400,
                int queue_max_size = 10000,
                int queue_shrink_size = 9900) -> std::unique_ptr<OStat>;

extern auto influx_file(const std::string& filename, 
                std::string global_tags = "",
                int pack_size = 400,
                int queue_max_size = 10000,
                int queue_shrink_size = 9900) -> std::unique_ptr<OStat>;


#endif //__EPOLL_H__