#include <unistd.h>
#include <csignal>
#include <cstring>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <set>
#include <map>
#include <vector>
#include <initializer_list>
#include <utility>
#include <chrono>
using namespace std::chrono_literals;
//#include <filesystem>
#include <libudev.h> //dnf install systemd-devel; apt-get install libudev-dev

#include "log.h"
#include "err.h"
#include "loop.h"
#include "timer.h"

class Epoll {
public:
    Epoll() = default;

    auto create(int flags = 0) -> errno_c {
        efd = epoll_create1(flags);
        if (efd==-1) return errno_c("epoll_create");
        return errno_c(0);
    }
    ~Epoll() {
        if (efd != -1) close(efd);
    }
    auto add(int fd, uint32_t events, void* ptr = nullptr) -> errno_c {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev), "epoll_ctl add");
    }
    auto mod(int fd, uint32_t events, void* ptr = nullptr) -> errno_c {
        epoll_event ev;
        ev.events = events;
        if (ptr) { ev.data.ptr = ptr;
        } else { ev.data.fd = fd;
        }
        return err_chk(epoll_ctl(efd, EPOLL_CTL_MOD, fd, &ev), "epoll_ctl mod");
    }
    auto del(int fd) -> errno_c {
        epoll_event ev;
        return err_chk(epoll_ctl(efd, EPOLL_CTL_DEL, fd, &ev), "epoll_ctl del");
    }
    auto wait(epoll_event *events, int maxevents, int timeout = -1, const sigset_t *sigmask = nullptr) -> int {
        if (sigmask) return epoll_pwait(efd, events, maxevents, timeout, sigmask);
        return epoll_wait(efd, events, maxevents, timeout);
    }
private:
    int efd = -1;
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
    auto epollIN() -> int override {
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
    auto start_with(IOLoop* loop) -> error_c override {
        error_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, nullptr),"sigprocmask");
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

class UDevIO : public IOPollable, public error_handler {
public:
    using OnActionFunc = std::function<void(udev_device*)>;
    UDevIO():IOPollable("udev") {}
    void on_action(OnActionFunc func) {
        _on_action = func;
    }
    auto start_with(IOLoop* loop) -> error_c override {
        _udev = udev_new();
        if (!_udev) return errno_c("udev_new");
        _mon = udev_monitor_new_from_netlink(_udev, "udev");
        if (!_mon) return errno_c("udev_monitor_new_from_netlink");
        int ret = udev_monitor_filter_add_match_subsystem_devtype(_mon,"tty",nullptr);
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
    auto epollIN() -> int override {
        udev_device *dev = udev_monitor_receive_device(_mon);
        if (!dev) {
            errno_c err("udev_monitor_receive_device");
            on_error(err);
        } else if (_on_action) _on_action(dev);
        return HANDLED;
    }
    auto find_link(const std::string& path) -> std::string {
        std::string ret;
        udev_enumerate *enumerate = udev_enumerate_new(_udev);
        if (!enumerate) return ret;
        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);
        udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry *dev_list_entry;
        bool device_found = false;
        udev_list_entry_foreach(dev_list_entry, devices) {
            udev_device *dev = udev_device_new_from_syspath(_udev, udev_list_entry_get_name(dev_list_entry));
            struct udev_list_entry *list_entry;
            std::string dev_id;
            udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
                std::string link = udev_list_entry_get_name(list_entry);
                if (link.rfind("/dev/serial/by-id/",0)==0) {
                    dev_id = link;
                }
                //std::filesystem::path link = udev_list_entry_get_name(list_entry);
                //if (link.parent_path()=="/dev/serial/by-id/") {
                //    dev_id = link;
                //}

                if (!device_found) device_found = link==path;
            }
            if (device_found) {
                ret = dev_id;
                break;
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumerate);
        return ret;
    }
    auto return_id(udev_device *dev) -> std::string {
        std::string ret;
        udev_list_entry *list_entry;
        udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
            std::string link = udev_list_entry_get_name(list_entry);
            if (link.rfind("/dev/serial/by-id/",0)==0) {
                ret = link;
            }
        }
        return ret;
    }
    auto find_id(const std::string& path) -> std::string {
        std::string sysname = path;
        auto pos = sysname.rfind("/");
        if (pos!= std::string::npos) {
            sysname = sysname.substr(pos+1);
        }
        udev_device *dev = udev_device_new_from_subsystem_sysname(_udev,"tty",sysname.c_str());
        if (!dev) return find_link(path);
        return return_id(dev);
    }

    auto find_path(const std::string& id) -> std::string {
        std::string ret;
        udev_enumerate *enumerate = udev_enumerate_new(_udev);
        if (!enumerate) return ret;
        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);
        udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry *dev_list_entry;
        bool device_found = false;
        udev_list_entry_foreach(dev_list_entry, devices) {
            udev_device *dev = udev_device_new_from_syspath(_udev, udev_list_entry_get_name(dev_list_entry));
            struct udev_list_entry *list_entry;
            std::string dev_id;
            udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
                std::string link = udev_list_entry_get_name(list_entry);
                if (!device_found) device_found = link==id;
                if (device_found) break;
            }
            if (device_found) {
                ret = udev_device_get_devnode(dev);
                break;
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumerate);
        return ret;
    }

private:
    udev *_udev = nullptr;
    udev_monitor *_mon = nullptr;
    int _fd = -1;
    OnActionFunc _on_action;
};

class OStatSet : public OStat {
public:
    void send(Metric&& metric) override {
        for(auto& ostat:ostats) ostat->send(std::forward<Metric>(metric));
    }
    void flush() override {
        for(auto& ostat:ostats) ostat->flush();
    }
    std::set<std::unique_ptr<OStat>> ostats;
};

class PeriodicStatCall {
public:
    PeriodicStatCall() {
        _timer.on_shoot_func([this](){
            if (!_sink) {
                stop();
                return;
            }
            for(auto stat: _stats) { stat->report(*_sink);
            }
            _sink->flush();
        });
        _timer.on_error([](const error_c& ec) {
            std::cout<<"metrics timer error:"<<ec<<std::endl;
        });
    };
    void setup(std::chrono::nanoseconds period, IOLoop* loop) {
        _loop = loop;
        _period = period;
    }
    void start() {
        if (_is_started) return;
        error_c ec = _timer.start_with(_loop);
        if (ec) { 
            log::error()<<"Error starting metrics timer: "<<ec<<std::endl;
            return;
        }
        ec = _timer.arm_periodic(_period);
        if (ec) { 
            log::error()<<"Error arming metrics timer: "<<ec<<std::endl;
            return;
        }
        _is_started = true;
    }
    void stop() {
        if (!_is_started) return;
        _timer.stop();
        _is_started = false;
    }
    void add(Stat* source, OStatSet* sink) {
        _stats.insert(source);
        _sink = sink;
        if (!_is_started) start();
    }
    void remove(Stat* source) {
        _stats.erase(source);
        if (_stats.size()==0 && _is_started) { stop();
        }
    }

    bool _is_started = false;
    Timer _timer;
    OStatSet* _sink = nullptr;
    IOLoop* _loop = nullptr;
    std::set<Stat*> _stats;
    std::chrono::nanoseconds _period;
};

class LoopStat : public Stat {
public:
    void report(OStat& out) override {
        Metric meter("loop");
        for(auto& item : time_measure) {
            item.second.report(meter,item.first);
        }
        out.send(std::move(meter));
    }
    std::map<std::string,DurationCollector> time_measure;
};


class AvahiWatch : public IOPollable {
public:
    AvahiWatch(IOLoop* loop, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata)
        :IOPollable("AvahiWatch"), _loop(loop), _fd(fd),
         _callback(callback),_userdata(userdata) {
        _loop->add(_fd,avahi2epoll_flags(event),this);
    }

    auto epollEvent(int event) -> bool override {
        _event = epoll2avahi_flags(event);
        //log::debug()<<"AvahiLoop watch callback "<<_fd<<" "<<counter++<<std::endl;
        _callback(this,_fd,_event,_userdata);
        return true;
    }

    static auto avahi2epoll_flags(AvahiWatchEvent flags) -> int {
        int ret = 0;
        if (flags & AVAHI_WATCH_IN)
            ret |= EPOLLIN;
        if (flags & AVAHI_WATCH_OUT)
            ret |= EPOLLOUT;
        if (flags & AVAHI_WATCH_ERR)
            ret |= EPOLLERR;
        if (flags & AVAHI_WATCH_HUP)
            ret |= EPOLLHUP | EPOLLRDHUP;
        return ret;
    }

    static auto epoll2avahi_flags(int flags) -> AvahiWatchEvent {
        int ret = 0;
        if (flags & EPOLLIN)
            ret |= AVAHI_WATCH_IN;
        if (flags & EPOLLOUT)
            ret |= AVAHI_WATCH_OUT;
        if (flags & EPOLLERR)
            ret |= AVAHI_WATCH_ERR;
        if (flags & EPOLLHUP | EPOLLRDHUP)
            ret |= AVAHI_WATCH_HUP;
        return (AvahiWatchEvent)ret;
    }

    static auto watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata) -> AvahiWatch* {
        auto loop = (IOLoop*)api->userdata;
        //log::debug()<<"AvahiLoop watch new "<<fd<<" event "<<event<<std::endl;
        return new AvahiWatch(loop,fd,event,callback,userdata);
    }

    /** Update the events to wait for. It is safe to call this function from an AvahiWatchCallback */
    static void watch_update(AvahiWatch *w, AvahiWatchEvent event) {
        //log::debug()<<"AvahiLoop watch update "<<w->_fd<<std::endl;
        w->_loop->mod(w->_fd,avahi2epoll_flags(event), w);
    }

    /** Return the events that happened. It is safe to call this function from an AvahiWatchCallback  */
    static auto watch_get_events(AvahiWatch *w) -> AvahiWatchEvent {
        //log::debug()<<"AvahiLoop watch get_events "<<w->_fd<<std::endl;
        return w->_event;
    }

    /** Free a watch. It is safe to call this function from an AvahiWatchCallback */
    static void watch_free(AvahiWatch *w) {
        //log::debug()<<"AvahiLoop watch free "<<w->_fd<<std::endl;
        w->_loop->del(w->_fd, w);
        delete w;
    }

    IOLoop* _loop;
    int _fd;
    AvahiWatchEvent _event;
    AvahiWatchCallback _callback;
    int counter = 0;
    void *_userdata;
};

auto timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata) -> AvahiTimeout* {
    auto* loop = (IOLoop*)api->userdata;
    auto timer = new Timer();
    timer->on_shoot_func([timer,callback, userdata](){ 
        //log::debug()<<"AvahiLoop timeout callback "<<userdata<<std::endl;
        callback((AvahiTimeout *)timer,userdata);
    });
    //log::debug()<<"AvahiLoop timeout new "<<userdata<<" "<<timer<<std::endl;
    timer->start_with(loop);
    if (tv) {
        std::chrono::microseconds tmo(uint64_t(tv->tv_sec)*1000000+tv->tv_usec);
        //log::debug()<<"timeout "<<tv->tv_sec<<" "<<tv->tv_usec<<" "<<tmo.count()<<std::endl;
        if (tmo.count()==0) { timer->arm_oneshoot(1ns,false);
        } else { timer->arm_oneshoot(tmo,false);
        }   
    }
    return (AvahiTimeout*) timer;
}

void timeout_update(AvahiTimeout * t, const struct timeval *tv) {
    auto timer = (Timer*)t;
    //log::debug()<<"AvahiLoop timeout update "<<timer<<std::endl;
    if (tv) {
        std::chrono::microseconds tmo(uint64_t(tv->tv_sec)*1000000+tv->tv_usec);
        //log::debug()<<"timeout "<<tv->tv_sec<<" "<<tv->tv_usec<<" "<<tmo.count()<<std::endl;
        if (tmo.count()==0) { timer->arm_oneshoot(1ns,false);
        } else { timer->arm_oneshoot(tmo,false);
        }
    }
}

void timeout_free(AvahiTimeout *t) {
    auto timer = (Timer*)t;
    //log::debug()<<"AvahiLoop timeout free "<<timer<<std::endl;
    timer->stop();
    delete timer;
}

class IOLoop::IOLoopImpl {
public:
    IOLoopImpl(int size=8):_size(size) {
        errno_c ret = _epoll.create();
        if (ret) {
            ret.add_place("IOLoop");
            log::error()<<ret<<std::endl;
            throw std::system_error(ret, ret.place());
        }
        auto on_err = [this](error_c& ec){on_error(ec);};
        stop_signal.on_error(on_err);
        udev.on_error(on_err);
        avahi_poll.watch_new = AvahiWatch::watch_new;
        avahi_poll.watch_update = AvahiWatch::watch_update;
        avahi_poll.watch_get_events = AvahiWatch::watch_get_events;
        avahi_poll.watch_free = AvahiWatch::watch_free;
        avahi_poll.timeout_new = timeout_new;
        avahi_poll.timeout_update = timeout_update;
        avahi_poll.timeout_free = timeout_free;
    }
    void create_avahi_handler(IOLoop* loop) {
        if (!avahi) {
            avahi_poll.userdata = loop;
            avahi = AvahiHandler::create(&avahi_poll);
        }
    }
    void on_error(error_c& ec) {
        log::error()<<"io loop->"<<ec<<std::endl;
    }
    Epoll _epoll;
    Signal stop_signal;
    UDevIO udev;
    int _sig_fd;
    int _size;
    bool _stop = false;
    std::set<IOPollable*> watches;
    std::set<IOPollable*> udev_watches;
    OStatSet ostats;
    std::map<std::chrono::nanoseconds, PeriodicStatCall> statcalls;
    LoopStat stat;
    AvahiPoll avahi_poll;
    std::unique_ptr<AvahiHandler> avahi;
};

IOLoop::IOLoop(int size):_impl{new IOLoopImpl{size}} {}

IOLoop::~IOLoop() = default;

auto IOLoop::execute(IOPollable* obj) -> error_c {
    return obj->start_with(this);
}
auto IOLoop::add(int fd, uint32_t events, IOPollable* obj) -> errno_c {
    errno_c ret = _impl->_epoll.add(fd, events, obj);
    if (!ret) {
        _impl->watches.insert(obj);
    }
    return ret;
}
auto IOLoop::mod(int fd, uint32_t events, IOPollable* obj) -> errno_c {
    return _impl->_epoll.mod(fd, events, obj);
}
auto IOLoop::del(int fd, IOPollable* obj) -> errno_c {
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

auto IOLoop::udev_find_id(const std::string& path) -> std::string {
    return _impl->udev.find_id(path);
}

auto IOLoop::udev_find_path(const std::string& id) -> std::string {
    return _impl->udev.find_path(id);
}


auto IOLoop::run() -> int {
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
        _impl->ostats.send(Metric("udev").field("dev",1).tag("action",action).tag("node",node));
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
    register_report(&_impl->stat,100ms);
    ec = _impl->udev.start_with(this);
    if (ec) { _impl->on_error(ec);
    }
    std::vector<epoll_event> events(_impl->_size);
    while(!_impl->_stop) {
        int r = _impl->_epoll.wait(events.data(), events.size());
        if (r < 0) {
            errno_c err;
            if (err == std::error_condition(std::errc::interrupted)) { continue;
            } else {
                log::error()<<"IOLoop wait: "<<err.message()<<std::endl;
            }
        }
        for (int i = 0; i < r; i++) {
            auto* obj = static_cast<IOPollable *>(events[i].data.ptr);
            auto evs = events[i].events;
            //log::debug()<<obj->name<<" event "<<evs<<" obj "<<obj<<std::endl;
            if (obj->epollEvent(evs)) continue;
            if (evs & EPOLLIN) {
                //log::debug()<<"EPOLLIN"<<std::endl;
                int ret = obj->epollIN();
                auto s = _impl->stat.time_measure["in"].measure();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLIN not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLOUT) {
                //log::debug()<<"EPOLLOUT"<<std::endl;
                auto s = _impl->stat.time_measure["out"].measure();
                int ret = obj->epollOUT();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLOUT not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLPRI) {
                //log::debug()<<"EPOLLPRI"<<std::endl;
                auto s = _impl->stat.time_measure["pri"].measure();
                int ret = obj->epollPRI();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLPRI not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLERR) {
                //log::debug()<<"EPOLLERR"<<std::endl;
                auto s = _impl->stat.time_measure["err"].measure();
                int ret = obj->epollERR();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLERR not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLHUP) {
                //log::debug()<<"EPOLLHUP"<<std::endl;
                auto s = _impl->stat.time_measure["hup"].measure();
                int ret = obj->epollHUP();
                if (ret==IOPollable::NOT_HANDLED) log::warning()<<obj->name<<" EPOLLHUP not handled"<<std::endl;
                if (ret==IOPollable::STOP) continue;
            }
            if (evs & EPOLLRDHUP) {
                //log::debug()<<"EPOLLRDHUP"<<std::endl;
                auto s = _impl->stat.time_measure["rdhup"].measure();
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

void IOLoop::add_stat_output(std::unique_ptr<OStat> out) {
    bool ostats_empty = _impl->ostats.ostats.empty();
    _impl->ostats.ostats.insert(std::move(out));
    if (ostats_empty && !_impl->statcalls.empty()) {
        for(auto& statcall: _impl->statcalls) { statcall.second.start();
        }
    }
}
void IOLoop::clear_stat_outputs() {
    for(auto& statcall: _impl->statcalls) {
        statcall.second.stop();
    }
    _impl->ostats.ostats.clear();
}
void IOLoop::register_report(Stat* source, std::chrono::nanoseconds period) {
    auto& statcall = _impl->statcalls[period];
    statcall.setup(period, this);
    statcall.add(source, &_impl->ostats);
}
void IOLoop::unregister_report(Stat* source) {
    for(auto& statcall: _impl->statcalls) {
        statcall.second.remove(source);
    }
}

auto IOLoop::query_service(CAvahiService pattern, AvahiLookupFlags flags) -> std::unique_ptr<AvahiQuery>{
    _impl->create_avahi_handler(this);
    return _impl->avahi->query_service(pattern,flags);
}
auto IOLoop::get_register_group() -> std::unique_ptr<AvahiGroup> {
    _impl->create_avahi_handler(this);
    return _impl->avahi->get_register_group();
}

