#ifndef __ZEROCONF_IMPL_H__
#define __ZEROCONF_IMPL_H__

#include "../loop.h"
#include "avahi-poll.h"

class AvahiImpl : public Avahi {
public:
    AvahiImpl(IOLoopSvc* loop) {
        create_avahi_poll(loop, avahi_poll);
        handler = AvahiHandler::create(&avahi_poll);
    }
    auto query_service(CAvahiService pattern, AvahiLookupFlags flags=(AvahiLookupFlags)0) -> std::unique_ptr<AvahiQuery> override {
        return handler->query_service(pattern, flags);
    }
    auto get_register_group() -> std::unique_ptr<AvahiGroup> override {
        return handler->get_register_group();
    }
    std::unique_ptr<AvahiHandler> handler;
    AvahiPoll avahi_poll;
};

#endif  //!__ZEROCONF_IMPL_H__