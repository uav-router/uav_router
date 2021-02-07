#ifndef __AVAHI_CPP_H__
#define __AVAHI_CPP_H__
#include <net/if.h>
#include <functional>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/address.h>
#include "err.h"

struct CAvahiService {
    AvahiIfIndex interface=AVAHI_IF_UNSPEC;
    AvahiProtocol protocol=AVAHI_PROTO_UNSPEC;
    std::string name;
    std::string type;
    std::string domain;
    CAvahiService() = default;
    CAvahiService(std::string n, std::string t, std::string d = std::string(), AvahiIfIndex i = AVAHI_IF_UNSPEC, AvahiProtocol p = AVAHI_PROTO_UNSPEC) : 
        interface(i), protocol(p), name(std::move(n)), type(std::move(t)), domain(std::move(d)) {}
    auto set_type(std::string t) -> CAvahiService&& {
        type = std::move(t);
        return std::move(*this);
    }
    auto set_ipv4() -> CAvahiService&& {
        protocol = AVAHI_PROTO_INET;
        return std::move(*this);
    }
    auto set_ipv6() -> CAvahiService&& {
        protocol = AVAHI_PROTO_INET6;
        return std::move(*this);
    }
    auto set_interface(std::string if_name) -> CAvahiService&& {
        auto if_ = if_nametoindex(if_name.c_str());
        if (if_) interface = if_;
        return std::move(*this);
    }
    auto get_interface() -> std::string {
        return if_indextoname(interface,(char*)alloca(IF_NAMESIZE));
    }
};

class AvahiQuery {
public:
    using OnResolve = std::function<void(
        CAvahiService service, 
        std::string host_name,
        const sockaddr_storage& addr,
        std::vector<std::pair<std::string,std::string>> txt,
        AvahiLookupResultFlags flags)>;
    using OnRemove = std::function<void(
        CAvahiService service, 
        AvahiLookupResultFlags flags)>;
    using OnFailure = std::function<void(error_c)>;
    using OnEvent = std::function<void()>;
    virtual ~AvahiQuery() = default;
    virtual void on_resolve(OnResolve func) = 0;
    virtual void on_remove(OnRemove func) = 0;
    virtual void on_failure(OnFailure func) = 0;
    virtual void on_complete(OnEvent func) = 0;
};

class AvahiGroup {
public:
    using OnEvent = std::function<void(AvahiGroup*)>;
    using OnFailure = std::function<void(error_c)>;
    virtual ~AvahiGroup() = default;
    virtual void on_create(OnEvent func) = 0;
    virtual void on_collision(OnEvent func) = 0;
    virtual void on_established(OnEvent func) = 0;
    virtual void on_failure(OnFailure func) = 0;
    virtual void create() = 0;

    virtual auto reset() -> error_c = 0;
    virtual auto commit() -> error_c = 0;
    virtual auto state() -> int = 0;

    virtual auto is_empty() -> int = 0;
    virtual auto add_service(CAvahiService item,
                    uint16_t port,
                    std::string subtype = "",
                    std::initializer_list<std::pair<std::string,std::string>> txt = {},
                    std::string host = std::string(),
                    AvahiPublishFlags flags = (AvahiPublishFlags) 0
                    ) -> error_c = 0;
    virtual auto host_name() -> std::string = 0;
};

class AvahiHandler {
public:
    virtual ~AvahiHandler() = default;
    virtual auto query_service(CAvahiService pattern, AvahiLookupFlags flags=(AvahiLookupFlags)0) -> std::unique_ptr<AvahiQuery> = 0;
    virtual auto get_register_group() -> std::unique_ptr<AvahiGroup> = 0;
    static auto create(const AvahiPoll* poll, AvahiClientFlags flags = (AvahiClientFlags)0) -> std::unique_ptr<AvahiHandler>;
};

#endif