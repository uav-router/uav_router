#ifndef __AVAHI_POLL__H__
#define __AVAHI_POLL__H__
#include <chrono>
using namespace std::chrono_literals;

#include <sys/epoll.h>
#include "../loop.h"
#include "../avahi-cpp.h"

class AvahiWatch : public IOPollable {
public:
    AvahiWatch(IOLoopSvc* loop, int fd, AvahiWatchEvent event, AvahiWatchCallback callback, void *userdata)
        :IOPollable("AvahiWatch"), _poll(loop->poll()), _fd(fd),
         _callback(callback),_userdata(userdata) {
        _poll->add(_fd,avahi2epoll_flags(event),this);
    }

    auto epollEvent(int event) -> bool override {
        _event = epoll2avahi_flags(event);
        //log.debug()<<"AvahiLoop watch callback "<<_fd<<" "<<counter++<<Log::endl;
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
        auto loop = (IOLoopSvc*)api->userdata;
        //log.debug()<<"AvahiLoop watch new "<<fd<<" event "<<event<<Log::endl;
        return new AvahiWatch(loop,fd,event,callback,userdata);
    }

    /** Update the events to wait for. It is safe to call this function from an AvahiWatchCallback */
    static void watch_update(AvahiWatch *w, AvahiWatchEvent event) {
        //log.debug()<<"AvahiLoop watch update "<<w->_fd<<Log::endl;
        w->_poll->mod(w->_fd,avahi2epoll_flags(event), w);
    }

    /** Return the events that happened. It is safe to call this function from an AvahiWatchCallback  */
    static auto watch_get_events(AvahiWatch *w) -> AvahiWatchEvent {
        //log.debug()<<"AvahiLoop watch get_events "<<w->_fd<<Log::endl;
        return w->_event;
    }

    /** Free a watch. It is safe to call this function from an AvahiWatchCallback */
    static void watch_free(AvahiWatch *w) {
        //log.debug()<<"AvahiLoop watch free "<<w->_fd<<Log::endl;
        w->_poll->del(w->_fd, w);
        delete w;
    }

    Poll* _poll;
    int _fd;
    AvahiWatchEvent _event;
    AvahiWatchCallback _callback;
    int counter = 0;
    void *_userdata;
};

namespace AvahiTimer {
    auto timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback callback, void *userdata) -> AvahiTimeout* {
        auto* loop = (IOLoopSvc*)api->userdata;
        auto timer = loop->timer().release();
        timer->shoot([timer,callback, userdata](){ 
            //log.debug()<<"AvahiLoop timeout callback "<<userdata<<Log::endl;
            callback((AvahiTimeout *)timer,userdata);
        });
        //log.debug()<<"AvahiLoop timeout new "<<userdata<<" "<<timer<<Log::endl;
        if (tv) {
            std::chrono::microseconds tmo(uint64_t(tv->tv_sec)*1000000+tv->tv_usec);
            //log.debug()<<"timeout "<<tv->tv_sec<<" "<<tv->tv_usec<<" "<<tmo.count()<<Log::endl;
            if (tmo.count()==0) { timer->arm_oneshoot(1ns);
            } else { timer->arm_oneshoot(tmo);
            }   
        }
        return (AvahiTimeout*) timer;
    }

    void timeout_update(AvahiTimeout * t, const struct timeval *tv) {
        auto timer = (Timer*)t;
        //log.debug()<<"AvahiLoop timeout update "<<timer<<Log::endl;
        if (tv) {
            std::chrono::microseconds tmo(uint64_t(tv->tv_sec)*1000000+tv->tv_usec);
            //log.debug()<<"timeout "<<tv->tv_sec<<" "<<tv->tv_usec<<" "<<tmo.count()<<Log::endl;
            if (tmo.count()==0) { timer->arm_oneshoot(1ns);
            } else { timer->arm_oneshoot(tmo);
            }
        }
    }

    void timeout_free(AvahiTimeout *t) {
        auto timer = (Timer*)t;
        //log.debug()<<"AvahiLoop timeout free "<<timer<<Log::endl;
        timer->stop();
        delete timer;
    }
}

void create_avahi_poll(IOLoopSvc *ioloop, AvahiPoll& poll) {
    poll.watch_new = AvahiWatch::watch_new;
    poll.watch_update = AvahiWatch::watch_update;
    poll.watch_get_events = AvahiWatch::watch_get_events;
    poll.watch_free = AvahiWatch::watch_free;
    poll.timeout_new = AvahiTimer::timeout_new;
    poll.timeout_update = AvahiTimer::timeout_update;
    poll.timeout_free = AvahiTimer::timeout_free;
    poll.userdata = ioloop;
}

#endif  //!__AVAHI_POLL__H__