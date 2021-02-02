#ifndef __AVAHI_COMMON_H__
#define __AVAHI_COMMON_H__
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <functional>
#include <string>

class CAvahiTimeout {
public:
    virtual void update(const struct timeval *tv) = 0;
    virtual void free() = 0;
};

class CAvahiPoll {
public:
    using TimeoutFunc = std::function<void(CAvahiTimeout*)>;
    virtual ~CAvahiPoll() = default;
    virtual const AvahiPoll* get() = 0;
    virtual int loop() = 0;
    virtual void quit() = 0;
    virtual void on_timeout(const struct timeval *tv, TimeoutFunc func) = 0;
};

class CAvahiSimplePoll : public CAvahiPoll {
public:
    CAvahiSimplePoll();
    ~CAvahiSimplePoll();
    const AvahiPoll* get() override;
    int loop() override;
    void quit() override;
    void on_timeout(const struct timeval *tv, TimeoutFunc func) override;
private:
    AvahiSimplePoll *_poll;
};

class CAvahiClient {
public:
    using OnStateChanged = std::function<void(CAvahiClient* , AvahiClientState)>;
    using OnFailure = std::function<void(CAvahiClient*)>;
    int init(const AvahiPoll* poll, AvahiClientFlags flags = (AvahiClientFlags)0);
    void on_state_changed(OnStateChanged func);
    void on_failure(OnFailure func);
    ~CAvahiClient();
    const char* error();
    AvahiClient * get();
    AvahiClientState state();
private:
    static void callback(AvahiClient *s, AvahiClientState state, void* userdata );

    OnStateChanged _on_state;
    OnFailure _on_failure;
    AvahiClient* _client = nullptr;
};

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

class CAvahiServiceBrowser {
public:
    using OnLookup = std::function<void(CAvahiServiceBrowser* , CAvahiService,
        AvahiLookupResultFlags)>;
    using OnEvent = std::function<void(CAvahiServiceBrowser*)>;
    
    int init(const char *type, AvahiClient* client, CAvahiService item=CAvahiService(), 
        AvahiLookupFlags flags= (AvahiLookupFlags)0
    );
    void on_new(OnLookup func);
    void on_remove(OnLookup func);
    void on_failure(OnEvent func);
    void on_all_for_now(OnEvent func);
    void on_cache_exhausted(OnEvent func);
    ~CAvahiServiceBrowser();
    const char* error();
    AvahiClient * client();
    AvahiServiceBrowser * get();
private:
    static void callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                         AvahiProtocol protocol, AvahiBrowserEvent event,
                         const char *name,       const char *type,
                         const char *domain,     AvahiLookupResultFlags flags,
                         void *userdata );
    OnLookup _on_new;
    OnLookup _on_remove;
    OnEvent _on_failure;
    OnEvent _on_all_for_now;
    OnEvent _on_cache_exhausted;
    AvahiServiceBrowser* _sb = nullptr;
};

class CAvahiServiceResolver {
public:
    using OnResolve = std::function<void(CAvahiServiceResolver*,
        CAvahiService item, 
        const char *host_name, const AvahiAddress *a, uint16_t port,
        AvahiStringList *txt, AvahiLookupResultFlags flags)>;
    
    void on_resolve(OnResolve func);
    void on_failure(OnResolve func);
    bool init(AvahiClient * client, CAvahiService item, 
            AvahiProtocol aprotocol = AVAHI_PROTO_UNSPEC, AvahiLookupFlags flags = (AvahiLookupFlags) 0);
    ~CAvahiServiceResolver();
    void free();
    const char* error();
    AvahiServiceResolver * get();
private:
    static void callback(AvahiServiceResolver *r,
                            AvahiIfIndex interface,
                            AvahiProtocol protocol,
                            AvahiResolverEvent event,
                            const char *name,
                            const char *type,
                            const char *domain,
                            const char *host_name,
                            const AvahiAddress *a,
                            uint16_t port,
                            AvahiStringList *txt,
                            AvahiLookupResultFlags flags,
                            void *userdata );
    OnResolve _on_resolve;
    OnResolve _on_failure;
    AvahiServiceResolver* _sr = nullptr;
};

class CAvahiEntryGroup {
public:
    using OnEvent = std::function<void(CAvahiEntryGroup*)>;
    void init(AvahiClient * client);
    void on_uncommited(OnEvent func);
    void on_registering(OnEvent func);
    void on_established(OnEvent func);
    void on_collision(OnEvent func);
    void on_failure(OnEvent func);
    const char* error();
    bool is_empty();
    bool is_init();
    int add_service(CAvahiService item,
                    uint16_t port,
                    const char *host = nullptr,
                    AvahiPublishFlags flags = (AvahiPublishFlags) 0
                    );
    int add_service(CAvahiService item,
                    uint16_t port,
                    std::initializer_list<std::string> txt,
                    const char *host = nullptr,
                    AvahiPublishFlags flags = (AvahiPublishFlags) 0
                    );
    int add_service_subtype(CAvahiService item,
                            const char *subtype,
                            AvahiPublishFlags flags = (AvahiPublishFlags) 0);
    int commit();
    int reset();
private:
    static void callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *_userdata);
    OnEvent _on_uncommited;
    OnEvent _on_registering;
    OnEvent _on_established;
    OnEvent _on_collision;
    OnEvent _on_failure;

    AvahiEntryGroup *_group = nullptr;
};

#endif //__AVAHI_COMMON_H__