#ifndef __ZEROCONF_IMPL_H__
#define __ZEROCONF_IMPL_H__

#include <map>
#include <forward_list>
#include <map>
#include <set>
#include <regex>
#include <memory>
#include <climits>
#include <sys/socket.h>
#include "../loop.h"
#include "../log.h"
#include "avahi-poll.h"

class ServiceListener {
public:
    ServiceListener(Avahi* avahi, std::string type, bool check_ports=false):_avahi(avahi),_check_ports(check_ports) {
        _sb = _avahi->query_service(CAvahiService("(.*)",type));
        _sb->on_resolve([this](CAvahiService service, std::string host_name,
        SockAddr addr, std::vector<std::pair<std::string,std::string>> txt,
        AvahiLookupResultFlags flags){ 
            auto svcptr = _service.find(service.name);
            if (svcptr==_service.end()) {
                _service[service.name] = addr;
            } else {
                svcptr->second.push_front(addr);
            }
            log.info()<<"New service "<<service.name<<" "<<addr.format(SockAddr::REG_SERVICE)<<std::endl;
            std::string name;
            for(auto& rec: txt) {
                if (rec.first=="endpoint") name = rec.second;
            }
            if (name.empty()) {
                names[addr]=service.name;
            } else {
                names[addr]=name;
                aliases[name]=service.name;
            }
            if (_check_ports) {
                std::regex rex("^([\\d]+)-(.*?)-claim$");
                std::smatch result;
                if (std::regex_search(service.name,result,rex)) {
                    int port = stoi(result[1]);
                    if (port<=USHRT_MAX) {
                        ports[result[2]].insert(port);
                    }
                }
            }
            auto it = _watches.before_begin();
            for(auto p = _watches.begin();p!=_watches.end();p=std::next(it)) {
                if (p->expired()) {
                    p = _watches.erase_after(it);
                    continue;
                }
                p->lock()->svc_resolved(service.name,name,addr);
                it = p;
            }
        });
        _sb->on_complete([this](){
            complete = true;
            if (_on_complete) _on_complete();
        });
        _sb->on_remove([this](CAvahiService service, AvahiLookupResultFlags flags) {
            log.info()<<"Remove service "<<service.name<<std::endl;
            auto it = _watches.before_begin();
            for(auto p = _watches.begin();p!=_watches.end();p=std::next(it)) {
                if (p->expired()) {
                    p = _watches.erase_after(it);
                    continue;
                }
                p->lock()->svc_removed(service.name);
                it = p;
            }
            if (_check_ports) {
                std::regex rex("^([\\d]+)-(.*?)-claim$");
                std::smatch result;
                if (std::regex_search(service.name,result,rex)) {
                    int port = stoi(result[1]);
                    if (port<=USHRT_MAX) {
                        ports[result[2]].erase(port);
                    }
                }
            }
            auto svcptr = _service.find(service.name);
            if (svcptr==_service.end()) return;
            std::string endpoint_name;
            for(auto& addr : svcptr->second) {
                endpoint_name = names[addr];
                names.erase(addr);
            }
            _service.erase(svcptr);
            if (!endpoint_name.empty()) { aliases.erase(endpoint_name);
            }
        });
    }

    auto port_claimed(uint16_t port, SockAddr& addr) -> bool {
        auto thisports = ports.find(addr.format(SockAddr::IPADDR_ONLY));
        if (thisports==ports.end()) return false;
        return thisports->second.find(port)!=thisports->second.end();
    }

    void start_watch(std::shared_ptr<ServiceEvents>& obj) {
        _watches.push_front(obj);
    }

    void on_complete(AvahiQuery::OnEvent func) {
        _on_complete = func;
    }

    std::map<SockAddr,std::string> names;
    std::map<std::string,std::string> aliases;
    std::map<std::string,std::set<uint16_t>> ports;
    bool complete = false;
private:
    std::unique_ptr<AvahiQuery> _sb;
    Avahi* _avahi;
    std::map<std::string,SockAddrList> _service;
    std::forward_list<std::weak_ptr<ServiceEvents>> _watches;
    AvahiQuery::OnEvent _on_complete;
    bool _check_ports = false;
    
    inline static Log::Log log {"svclistener"};
};
class AvahiImpl : public Avahi {
public:
    AvahiImpl(IOLoopSvc* loop) {
        create_avahi_poll(loop, avahi_poll);
        handler = AvahiHandler::create(&avahi_poll);
        _tcp = std::make_unique<ServiceListener>(this,"_pktstreamnames._tcp");
        _udp = std::make_unique<ServiceListener>(this,"_pktstreamnames._udp",true);
        auto complete_func = [this]() {
            if (_tcp->complete && _udp->complete && _on_ready) _on_ready();
        };
        _tcp->on_complete(complete_func);
        _udp->on_complete(complete_func);
    }
    void on_ready(OnEvent func) override {
        _on_ready = func;
        if (_tcp->complete && _udp->complete) _on_ready();
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
    auto query_service_name(SockAddr& addr, int type) -> std::pair<std::string,std::string> override {
        ServiceListener *list = nullptr;
        if (type==SOCK_STREAM) { list = _tcp.get();
        }
        if (type==SOCK_DGRAM) { list = _udp.get();
        }
        if (list) {
            auto endpointptr = list->names.find(addr);
            if (endpointptr!=list->names.end()) {
                auto nameptr = list->aliases.find(endpointptr->second);
                if (nameptr!=list->aliases.end()) { 
                    return std::make_pair(nameptr->second,endpointptr->second);
                }
                return std::make_pair(endpointptr->second,endpointptr->second);
            }
        }
        return std::make_pair(std::string(),std::string());
    }
    void watch_services(std::shared_ptr<ServiceEvents>& obj, int type) override {
        ServiceListener *list = nullptr;
        if (type==SOCK_STREAM) { list = _tcp.get();
        }
        if (type==SOCK_DGRAM) { list = _udp.get();
        }
        if (list) { list->start_watch(obj);
        }
    }

    auto port_claimed(uint16_t port, SockAddr& addr) -> bool override {
        return _udp->port_claimed(port,addr);
    }

    std::unique_ptr<AvahiHandler> handler;
    std::unique_ptr<ServiceListener> _tcp;
    std::unique_ptr<ServiceListener> _udp;
    AvahiPoll avahi_poll;
    OnEvent _on_ready;
};

#endif  //!__ZEROCONF_IMPL_H__