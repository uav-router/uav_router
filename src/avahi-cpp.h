#ifndef __AVAHI_CPP_H__
#define __AVAHI_CPP_H__
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include "err.h"

struct CAvahiService {
    AvahiIfIndex interface=AVAHI_IF_UNSPEC;
    AvahiProtocol protocol=AVAHI_PROTO_UNSPEC;
    const char *name=nullptr;
    const char *type=nullptr;
    const char *domain=nullptr;
    CAvahiService() = default;
    CAvahiService(AvahiIfIndex i, AvahiProtocol p, const char *d) : 
        interface(i), protocol(p), domain(d) {}
    CAvahiService(const char *n, const char *t, const char *d = nullptr, AvahiIfIndex i = AVAHI_IF_UNSPEC, AvahiProtocol p = AVAHI_PROTO_UNSPEC) : 
        interface(i), protocol(p), name(n), type(t), domain(d) {}
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
                    const char *host = nullptr,
                    AvahiPublishFlags flags = (AvahiPublishFlags) 0
                    ) -> error_c = 0;
    
};

class AvahiHandler {
public:
    virtual ~AvahiHandler() = default;
    virtual auto query_service(CAvahiService pattern, AvahiLookupFlags flags) -> std::unique_ptr<AvahiQuery> = 0;
    virtual auto get_register_group() -> std::unique_ptr<AvahiGroup> = 0;
    static auto create(const AvahiPoll* poll, AvahiClientFlags flags = (AvahiClientFlags)0) -> std::unique_ptr<AvahiHandler>;
};

#endif