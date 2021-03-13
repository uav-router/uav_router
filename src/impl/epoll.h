#ifndef __EPOLL_H__
#define __EPOLL_H__

#include <unistd.h>
#include <sys/epoll.h>

#include "../err.h"

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

#endif //__EPOLL_H__