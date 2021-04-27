#ifndef __ZEROCONF_INC_H__
#define __ZEROCONF_INC_H__
#include "../avahi-cpp.h"

class ServiceEvents {
public:
    virtual ~ServiceEvents() = default;
    virtual void svc_resolved(std::string name, std::string endpoint, const SockAddr& addr) = 0;
    virtual void svc_removed(std::string name) = 0;
};
class Avahi : public AvahiHandler {
public:
    using OnEvent = AvahiQuery::OnEvent;
    virtual void on_ready(OnEvent func) = 0;
    virtual auto query_service_name(SockAddr& addr, int type) -> std::pair<std::string,std::string> = 0;
    virtual void watch_services(std::shared_ptr<ServiceEvents>& obj, int type) = 0;
    virtual auto port_claimed(uint16_t port, SockAddr& addr) -> bool = 0;
};

#endif  //!__ZEROCONF_INC_H__