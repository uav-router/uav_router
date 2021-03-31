#ifndef __ZEROCONF_IMPL_H__
#define __ZEROCONF_IMPL_H__

#include <map>
#include <forward_list>
#include <memory>
#include <sys/socket.h>
#include "../loop.h"
#include "avahi-poll.h"

class ServiceListener {
public:
    ServiceListener(Avahi* avahi, std::string type):_avahi(avahi) {
        _sb = _avahi->query_service(CAvahiService("(.*)",type));
        _sb->on_resolve([this](CAvahiService service, std::string host_name,
        SockAddr addr, std::vector<std::pair<std::string,std::string>> txt,
        AvahiLookupResultFlags flags){ 
            auto svcptr = _service.find(service.name);
            if (svcptr==_service.end()) {
                _service[service.name] = std::forward_list<SockAddr>({addr});
            } else {
                svcptr->second.push_front(addr);
            }
            std::string name;
            for(auto& rec: txt) {
                if (rec.first=="endpoint") name = rec.second;
            }
            if (name.empty()) {
                name = service.name;
            }
            names[addr]=name;
        });
        _sb->on_remove([this](CAvahiService service, AvahiLookupResultFlags flags) {
            auto svcptr = _service.find(service.name);
            if (svcptr==_service.end()) return;
            for(auto& addr : svcptr->second) {
                names.erase(addr);
            }
            _service.erase(svcptr);
        });
    }
    std::map<SockAddr,std::string> names;

private:
    std::unique_ptr<AvahiQuery> _sb;
    Avahi* _avahi;
    std::map<std::string,std::forward_list<SockAddr>> _service;
};
class AvahiImpl : public Avahi {
public:
    AvahiImpl(IOLoopSvc* loop) {
        create_avahi_poll(loop, avahi_poll);
        handler = AvahiHandler::create(&avahi_poll);
        _tcp = std::make_unique<ServiceListener>(this,"_pktstreamnames._tcp");
        _udp = std::make_unique<ServiceListener>(this,"_pktstreamnames._udp");
    }
    auto query_service(CAvahiService pattern, AvahiLookupFlags flags=(AvahiLookupFlags)0) -> std::unique_ptr<AvahiQuery> override {
        return handler->query_service(pattern, flags);
    }
    auto get_register_group() -> std::unique_ptr<AvahiGroup> override {
        return handler->get_register_group();
    }
    auto query_host_name(SockAddr& addr, OnHostName callback, AvahiLookupFlags flags=(AvahiLookupFlags)0) -> error_c override {
        return handler->query_host_name(addr, callback, flags);
    }
    auto query_service_name(SockAddr& addr, int type) -> std::string override {
        ServiceListener *list = nullptr;
        if (type==SOCK_STREAM) { list = _tcp.get();
        }
        if (type==SOCK_DGRAM) { list = _udp.get();
        }
        if (list) {
            auto it = list->names.find(addr);
            if (it==list->names.end()) return addr.format(SockAddr::REG_SERVICE);
            return it->second;
        }
        return addr.format(SockAddr::REG_SERVICE);
    }
    std::unique_ptr<AvahiHandler> handler;
    std::unique_ptr<ServiceListener> _tcp;
    std::unique_ptr<ServiceListener> _udp;
    AvahiPoll avahi_poll;
};

#endif  //!__ZEROCONF_IMPL_H__