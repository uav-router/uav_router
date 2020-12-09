#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <set>

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

class IOLoop::IOLoopImpl {
public:
    IOLoopImpl(int size=8):_size(size),_stop(false) {
        errno_c ret = _epoll.create();
        if (ret) {
            ret.add_place("IOLoop");
            log::error()<<ret.place()<<": "<<ret.message()<<std::endl;
            throw std::system_error(ret, ret.place());
        }
    }
    Epoll _epoll;
    int _sig_fd;
    int _size;
    bool _stop;
    std::set<IOPollable*> watches;
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
int IOLoop::run() {
    log::debug()<<"run start"<<std::endl;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    errno_c ret = err_chk(sigprocmask(SIG_BLOCK, &mask, NULL));
    if (ret) {
        log::error()<<"IOLoop->sigprocmask: "<<ret.message()<<std::endl;
    } else {
        _impl->_sig_fd = signalfd(-1, &mask, SFD_NONBLOCK);
        if (_impl->_sig_fd == -1) {
            errno_c err;
            log::error()<<"IOLoop->signalfd: "<<err.message()<<std::endl;
        } else {
            ret = _impl->_epoll.add(_impl->_sig_fd, EPOLLIN);
            if (ret) {
                log::error()<<"IOLoop->sig_fd add: "<<ret.message()<<std::endl;
                close(_impl->_sig_fd);
            }
        }
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
            if (events[i].data.fd==_impl->_sig_fd) {
                if (events[i].events & EPOLLIN) {
                    signalfd_siginfo fdsi;
                    ssize_t s = read(_impl->_sig_fd, &fdsi, sizeof(fdsi));
                    if (s==-1) {
                        errno_c err;
                        if (err == std::error_condition(std::errc::resource_unavailable_try_again)) break;
                        log::error()<<"IOLoop->_sig_fd read: "<<err.message()<<std::endl;
                        break;
                    }
                    if (s != sizeof(fdsi)) {
                        log::error()<<"IOLoop->_sig_fd Wrong read size "<<s<<std::endl;
                        continue;
                    }
                    log::info()<<"Signal: "<<strsignal(fdsi.ssi_signo)<<std::endl;
                    for (auto w : _impl->watches) { w->cleanup();
                    }
                    _impl->_stop=true;
                }
            } else {
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
    }
    log::debug()<<"run end"<<std::endl;
    return 0;
}

void IOLoop::stop() { _impl->_stop=true; }
