#include <unistd.h>
#include <csignal>
#include <cstring>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <set>
#include <initializer_list>
#include <libudev.h>

#include "log.h"
#include "err.h"
#include "epoll.h"

class Epoll {
public:
    Epoll():efd(-1) {}

    errno_c create(int flags = 0) {
        efd = epoll_create1(flags);
        if (efd==-1) return errno_c("epoll_create");
        return errno_c(0);
    }
    ~Epoll() {
        if (efd != -1) close(efd);
    }
    errno_c add(int fd, uint32_t events, void* ptr = nullptr) {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev), "epoll_ctl add");
    }
    errno_c mod(int fd, uint32_t events, void* ptr = nullptr) {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev), "epoll_ctl mod");
    }
    errno_c del(int fd) {
        epoll_event ev;
        return err_chk(epoll_ctl(efd, EPOLL_CTL_DEL, fd, &ev), "epoll_ctl del");
    }
    int wait(epoll_event *events, int maxevents, int timeout = -1, const sigset_t *sigmask = nullptr) {
        if (sigmask) return epoll_pwait(efd, events, maxevents, timeout, sigmask);
        return epoll_wait(efd, events, maxevents, timeout);
    }
private:
    int efd;
};

class Signal : public IOPollable, public error_handler {
public:
    using OnSignalFunc  = std::function<bool(signalfd_siginfo*)>;
    Signal():IOPollable("signal") {
        sigemptyset(&mask);
    }
    void on_signal(OnSignalFunc func) {
        _on_signal = func;
    }
    int epollIN() override {
        while(true) {
            signalfd_siginfo fdsi;
            ssize_t s = read(_fd, &fdsi, sizeof(fdsi));
            if (s==-1) {
                errno_c err("signal read");
                if (err == std::error_condition(std::errc::resource_unavailable_try_again)) break;
                on_error(err);
                return HANDLED;
            } 
            if (s != sizeof(fdsi)) {
                log::error()<<"IOLoop->_sig_fd Wrong read size "<<s<<std::endl;
                return HANDLED;
            }
            if (_on_signal) {
                if (_on_signal(&fdsi)) break;
            }
        }
        return HANDLED;
    }
    void cleanup() override {
        if (_fd != -1) close(_fd);
    }
    error_c start_with(IOLoop* loop) override {
        error_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, NULL),"sigprocmask");
        if (ret) return ret;
        _fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (_fd==-1) {
            errno_c err("signal_fd");
            return err;
        }
        ret = loop->add(_fd, EPOLLIN, this);
        if (ret) { ret.add_place("loop add");
        }
        return ret;
    }
    void init(std::initializer_list<int> signals) {
        sigemptyset(&mask);
        for(auto signal: signals) {
            sigaddset(&mask, signal);
        }
    }
private:
    sigset_t mask;
    OnSignalFunc _on_signal;
    int _fd = -1;
};

class UDev : public IOPollable, public error_handler {
public:
    using OnActionFunc = std::function<void(udev_device*)>;
    UDev():IOPollable("udev") {}
    void on_action(OnActionFunc func) {
        _on_action = func;
    }
    error_c start_with(IOLoop* loop) override {
        _udev = udev_new();
        if (!_udev) return errno_c("udev_new");
        _mon = udev_monitor_new_from_netlink(_udev, "udev");
        if (!_mon) return errno_c("udev_monitor_new_from_netlink");
        int ret = udev_monitor_filter_add_match_subsystem_devtype(_mon,"tty",NULL);
        if (ret<0) return errno_c("udev_monitor_filter_add_match_subsystem_devtype");
        ret = udev_monitor_enable_receiving(_mon);
        if (ret<0) return errno_c("udev_monitor_enable_receiving");
        _fd = udev_monitor_get_fd(_mon);
        if (_fd<0) return errno_c("udev_monitor_get_fd");
        error_c ec = loop->add(_fd, EPOLLIN, this);
        return ec;
    }
    void cleanup() override {
        if (_udev) udev_unref(_udev);
        if (_mon) udev_monitor_unref(_mon);
        if (_fd >= 0) close(_fd);
    }
    int epollIN() override {
        udev_device *dev = udev_monitor_receive_device(_mon);
        if (!dev) {
            errno_c err("udev_monitor_receive_device");
            on_error(err);
        } else if (_on_action) _on_action(dev);
        return HANDLED;
    }
    std::string find_id(const std::string& path) {
        udev_device *dev = udev_device_new_from_subsystem_sysname(_udev,"tty",path.c_str());
        //if (!dev) return std::string();
        return std::string();
    }

    std::string find_path(const std::string& id) {
        return std::string();
    }

private:
    udev *_udev = nullptr;
    udev_monitor *_mon = nullptr;
    int _fd = -1;
    OnActionFunc _on_action;
};

class IOLoop::IOLoopImpl {
public:
    IOLoopImpl(int size=8):_size(size),_stop(false) {
        errno_c ret = _epoll.create();
        if (ret) {
            ret.add_place("IOLoop");
            log::error()<<ret.place()<<": "<<ret.message()<<std::endl;
            throw std::system_error(ret, ret.place());
        }
        auto on_err = [this](error_c& ec){on_error(ec);};
        stop_signal.on_error(on_err);
        udev.on_error(on_err);
    }
    void on_error(error_c& ec) {
        log::error()<<"io loop->"<<ec.place()<<": "<<ec.message()<<std::endl;
    }
    Epoll _epoll;
    Signal stop_signal;
    UDev udev;
    int _sig_fd;
    int _size;
    bool _stop;
    std::set<IOPollable*> watches;
    std::set<IOPollable*> udev_watches;
};

IOLoop::IOLoop(int size):_impl{new IOLoopImpl{size}} {}

IOLoop::~IOLoop() {}

error_c IOLoop::execute(IOPollable* obj) {
    return obj->start_with(this);
}
errno_c IOLoop::add(int fd, uint32_t events, IOPollable* obj) {
    errno_c ret = _impl->_epoll.add(fd, events, obj);
    if (!ret) {
        _impl->watches.insert(obj);
    }
    return ret;
}
errno_c IOLoop::mod(int fd, uint32_t events, IOPollable* obj) {
    return _impl->_epoll.mod(fd, events, obj);
}
errno_c IOLoop::del(int fd, IOPollable* obj) {
    errno_c ret = _impl->_epoll.del(fd);
    if (!ret) {
        _impl->watches.erase(obj);
    }
    return ret;
}
void IOLoop::udev_start_watch(IOPollable* obj) {
    _impl->udev_watches.insert(obj);
}
void IOLoop::udev_stop_watch(IOPollable* obj) {
    _impl->udev_watches.erase(obj);
}

std::string IOLoop::udev_find_id(const std::string& path) {
    return _impl->udev.find_id(path);
}

std::string IOLoop::udev_find_path(const std::string& id) {
    return _impl->udev.find_path(id);
}


int IOLoop::run() {
    log::debug()<<"run start"<<std::endl;
    _impl->stop_signal.init({SIGINT,SIGTERM});
    _impl->stop_signal.on_signal([this](signalfd_siginfo* si) {
        log::info()<<"Signal: "<<strsignal(si->ssi_signo)<<std::endl;
        for (auto w : _impl->watches) { w->cleanup();
        }
        _impl->_stop=true;
        return true;
    });
    error_c ec = _impl->stop_signal.start_with(this);
    if (ec) { _impl->on_error(ec);
    }
    _impl->udev.on_action([this](udev_device* dev){
        std::string action = udev_device_get_action(dev);
        std::string node = udev_device_get_devnode(dev);
        std::string link;
        udev_list_entry *list_entry;
        udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
            std::string l = udev_list_entry_get_name(list_entry);
            if (l.empty()) continue;
            if (l.rfind("/dev/serial/by-id/",0)==0) {
                link=l;
            }
        }
        for(auto& obj: _impl->udev_watches) {
            if (action=="add") {
                obj->udev_add(node,link);
            } else {
                obj->udev_remove(node,link);
            }
        }
    });
    ec = _impl->udev.start_with(this);
    if (ec) { _impl->on_error(ec);
    }
    epoll_event events[_impl->_size];
    while(!_impl->_stop) {
        int r = _impl->_epoll.wait(events, _impl->_size);
        if (r < 0) {
            errno_c err;
            if (err == std::error_condition(std::errc::interrupted)) { continue;
            } else {
                log::error()<<"IOLoop wait: "<<err.message()<<std::endl;
            }
        }
        for (int i = 0; i < r; i++) {
            IOPollable* obj = static_cast<IOPollable *>(events[i].data.ptr);
            auto evs = events[i].events;
            log::debug()<<obj->name<<" event "<<evs<<std::endl;
            if (obj->epollEvent(evs)) continue;
            if (evs & EPOLLIN) {
                log::debug()<<"EPOLLIN"<<std::endl;
                int ret = obj->epollIN();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLIN not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLOUT) {
                log::debug()<<"EPOLLOUT"<<std::endl;
                int ret = obj->epollOUT();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLOUT not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLPRI) {
                log::debug()<<"EPOLLPRI"<<std::endl;
                int ret = obj->epollPRI();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLPRI not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLERR) {
                log::debug()<<"EPOLLERR"<<std::endl;
                int ret = obj->epollERR();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLERR not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLHUP) {
                log::debug()<<"EPOLLHUP"<<std::endl;
                int ret = obj->epollHUP();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLHUP not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLRDHUP) {
                log::debug()<<"EPOLLRDHUP"<<std::endl;
                int ret = obj->epollRDHUP();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLRDHUP not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
        }
    }
    log::debug()<<"run end"<<std::endl;
    return 0;
}

void IOLoop::stop() { _impl->_stop=true; }
