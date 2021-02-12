#include <forward_list>
#include <cstring>
#include <memory>
#include <regex>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include "avahi-cpp.h"
#include "log.h"

class CAvahiClient {
public:
    using OnStateChanged = std::function<void(CAvahiClient* , AvahiClientState)>;
    using OnFailure = std::function<void(CAvahiClient*)>;
    void on_state_changed(OnStateChanged func) { _on_state = func; }
    void on_failure(OnFailure func) {           _on_failure = func;}
    ~CAvahiClient() { free(); }
    auto init(const AvahiPoll* poll, AvahiClientFlags flags = (AvahiClientFlags)0) -> error_c {
        int ret=0;
        _client = avahi_client_new(poll, flags, callback, this, &ret);
        if (ret) return avahi_code(ret,"avahi_client_new");
        return avahi_code(ret);
    }
    auto error(std::string place="") -> error_c {
        int ret = avahi_client_errno(_client);
        if (ret) return avahi_code(ret,place);
        return avahi_code(ret);
    }
    auto get() -> AvahiClient * { return _client; }
    auto state() -> AvahiClientState {
        return avahi_client_get_state(_client);
    }
    void free() {
        if (_client) {
            avahi_client_free(_client);
            _client = nullptr;
        }
    }
    auto host_name() -> std::string { return _host_name; }
private:
    static void callback(AvahiClient *s, AvahiClientState state, void* userdata) {
        auto* _this = (CAvahiClient*)userdata;
        if (_this->_client==nullptr) _this->_client = s;
        if (state==AVAHI_CLIENT_FAILURE) {
            if (_this->_on_failure) { _this->_on_failure(_this);
            }
            return;
        } else if (state==AVAHI_CLIENT_S_RUNNING) {
            _this->_host_name = avahi_client_get_host_name_fqdn(s);
            //log::debug()<<"Host: "<<avahi_client_get_host_name(s)<<" fqdn: "<<_this->_host_name;
            //log::debug()<<" domain: "<<avahi_client_get_domain_name(s)<<std::endl;
        }
        if (_this->_on_state) { _this->_on_state(_this, state);
        }    
    }

    OnStateChanged _on_state;
    OnFailure _on_failure;
    AvahiClient* _client = nullptr;
    std::string _host_name;
};

class CAvahiServiceBrowser {
public:
    using OnRemove = AvahiQuery::OnRemove;
    using OnEvent = AvahiQuery::OnEvent;
    using OnFailure = AvahiQuery::OnFailure;
    using OnResolve = AvahiQuery::OnResolve;
    /*using OnResolve = std::function<void(CAvahiServiceBrowser*,
        CAvahiService item, 
        const char *host_name, const AvahiAddress *a, uint16_t port,
        AvahiStringList *txt, AvahiLookupResultFlags flags)>;*/
    CAvahiServiceBrowser(CAvahiClient* c):_client(c),_service_pattern(".*"){};
    ~CAvahiServiceBrowser() {
        if (_sb) {
            log::debug()<<"avahi_service_browser_free"<<std::endl;
            avahi_service_browser_free(_sb);
        }
    }
    void init(CAvahiService item=CAvahiService(), AvahiLookupFlags flags= (AvahiLookupFlags)0) {
        _flags = flags;
        _sb = avahi_service_browser_new(
            _client->get(), item.interface, item.protocol, 
            item.type.c_str(), item.domain.c_str(), flags, callback, this );
        log::debug()<<"avahi_service_browser_new interface="<<item.interface<<" protocol="<<item.protocol<<" type="<<item.type;
        if (!item.domain.empty()) log::debug()<<" domain="<<item.domain;
        log::debug()<<" flags="<<flags<<" sb="<<_sb<<std::endl;
        try {
            if (!item.name.empty()) _service_pattern = item.name;
        } catch (std::regex_error &e) {
            log::error()<<regex_code(e.code())<<std::endl;
            _service_pattern = ".*";
        }
    }
    void on_resolve(OnResolve func) { _on_resolve = func; }
    void on_remove(OnRemove func) {_on_remove = func;}
    void on_failure(OnFailure func) {_on_failure = func;}
    void on_all_for_now(OnEvent func) {_on_all_for_now = func;}
    void on_cache_exhausted(OnEvent func) {_on_cache_exhausted = func;}

    auto error(std::string place) -> error_c {
        int ret = avahi_client_errno(avahi_service_browser_get_client(_sb));
        if (ret==0) return error_c(0);
        return avahi_code(ret,place);
    }
    auto client() -> AvahiClient * {
        return avahi_service_browser_get_client(_sb);
    }
    auto get() -> AvahiServiceBrowser * { return _sb;    
    }
    void on_failure(error_c ec) {
        if (_on_failure) _on_failure(ec);
    }
private:
    static void callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                         AvahiProtocol protocol, AvahiBrowserEvent event,
                         const char *name,       const char *type,
                         const char *domain,     AvahiLookupResultFlags flags,
                         void *userdata) {
        auto* _this = (CAvahiServiceBrowser*)userdata;
        log::debug()<<"avahi_service_browser_callback "<<event<<std::endl;
        switch(event) {
            case AVAHI_BROWSER_NEW: {
                if (!(avahi_service_resolver_new(_this->client(), interface, protocol, name, type, domain, 
                                                    protocol, (AvahiLookupFlags)0, resolve_callback, _this))) {
                    if (_this->_on_failure) _this->_on_failure(_this->error("service resolver new"));
                }
            } break;
            case AVAHI_BROWSER_REMOVE: {
                if (_this->_on_remove) {
                    try {
                        if (!std::regex_match(std::string(name),_this->_service_pattern)) {
                            log::info()<<"Reporting of the nonmatched service "<<name<<" REMOVE skipped"<<std::endl;
                            return;
                        }
                    } catch(std::regex_error& e) {
                        log::error()<<"Reporting of the service "<<name<<" REMOVE skipped: "<<regex_code(e.code())<<std::endl;
                    }
                    _this->_on_remove(CAvahiService{name,type,domain,interface,protocol},flags);
                }
            } break;
            case AVAHI_BROWSER_CACHE_EXHAUSTED: {
                if (_this->_on_cache_exhausted) _this->_on_cache_exhausted();
            } break;
            case AVAHI_BROWSER_ALL_FOR_NOW: {
                if (_this->_on_all_for_now) _this->_on_all_for_now();
            } break;
            case AVAHI_BROWSER_FAILURE: {
                if (_this->_on_failure) _this->_on_failure(_this->error("service browser"));
            }
        }
    }
    static void resolve_callback(AvahiServiceResolver *r,
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
                            void *userdata ) {
        auto* _this = (CAvahiServiceBrowser*)userdata;
        log::debug()<<"avahi_service_browser_resolve_callback "<<std::endl;
        if (event==AVAHI_RESOLVER_FAILURE) { 
            if (_this->_on_failure) 
                _this->_on_failure(
                    avahi_code( avahi_client_errno(avahi_service_resolver_get_client(r))
                               ,"service resolver callback")
                );
        } else if (event==AVAHI_RESOLVER_FOUND) {
            if (_this->_on_resolve) {
                try {
                    if (!std::regex_match(std::string(name),_this->_service_pattern)) {
                        log::info()<<"Reporting of the nonmatched service "<<name<<" ADD skipped"<<std::endl;
                        return;
                    }
                } catch(std::regex_error& e) {
                    log::error()<<"Reporting of the service "<<name<<" ADD skipped: "<<regex_code(e.code())<<std::endl;
                }
                std::vector<std::pair<std::string,std::string>> t;
                for (AvahiStringList * _t = txt; _t != nullptr; _t = avahi_string_list_get_next(_t)) {
                    char *key;
                    char *value;
                    if (avahi_string_list_get_pair(_t, &key, &value, nullptr)==0) {
                        t.push_back(std::make_pair<std::string,std::string>(key,value));
                        if (key) avahi_free(key);
                        if (value) avahi_free(value);
                    }
                }
                _this->_on_resolve( CAvahiService{name, type, domain, interface, protocol}, 
                    host_name, SockAddr{a,port}, t, flags);
            } 
        } else {
            //TODO: unknown func
        }
        avahi_service_resolver_free(r);
    }

    OnResolve _on_resolve;
    OnRemove _on_remove;
    OnFailure _on_failure;
    OnEvent _on_all_for_now;
    OnEvent _on_cache_exhausted;
    CAvahiClient* _client;
    std::regex _service_pattern;
    AvahiServiceBrowser* _sb = nullptr;
    AvahiLookupFlags _flags = (AvahiLookupFlags)0;
};

class AvahiQueryImpl: public AvahiQuery {
public:
    static std::forward_list<AvahiQueryImpl*> queries;

    AvahiQueryImpl(CAvahiClient* c, CAvahiService pattern, AvahiLookupFlags flags): _sb(c) {
        queries.push_front(this);
        _sb.init(pattern,flags); //!! can be null when creating browser
    }
    ~AvahiQueryImpl() override {
        queries.remove(this);
    }

    void on_resolve(OnResolve func) override {_sb.on_resolve(func);}
    void on_remove(OnRemove func)   override {_sb.on_remove(func);}
    void on_failure(OnFailure func) override {_sb.on_failure(func);}
    void on_complete(OnEvent func) override {_sb.on_all_for_now(func);}

    void on_client_failure(error_c ec) { _sb.on_failure(ec); }
    //void on_client_state_changed(AvahiClientState state) {
    //}
    CAvahiServiceBrowser _sb;    
};

std::forward_list<AvahiQueryImpl*> AvahiQueryImpl::queries;

class AvahiGroupImpl: public AvahiGroup {
public:
    static std::forward_list<AvahiGroupImpl*> queries;
    AvahiGroupImpl(CAvahiClient* c):_client(c) {}
    void on_create(OnEvent func) override {_on_create=func;}
    void on_collision(OnEvent func) override {_on_collision=func;}
    void on_established(OnEvent func) override {_on_established=func;}
    void on_failure(OnFailure func) override {_on_failure=func;}
    void create() override {
        if (_client->state()==AVAHI_CLIENT_S_RUNNING) {
            if (!_group) {
                _group = avahi_entry_group_new(_client->get(), callback, this);
                if (!_group && _on_failure) {
                    _on_failure(_client->error("avahi_entry_group_new"));
                    return;
                }
            }
            if (avahi_entry_group_is_empty(_group)>0 && _on_create) {
                _on_create(this);
            }
        }
    }
    void client_create() {
        create();
    }
    void client_reset() {
        if (_group && avahi_entry_group_is_empty(_group)==0) reset();
    }
    void on_client_failure(error_c ec) {
        if (_on_failure) _on_failure(ec);
    }
    auto reset() -> error_c override {
        int ret = avahi_entry_group_reset(_group);
        if (!ret) return error_c();
        return avahi_code(ret,"avahi_entry_group_commit");
    }
    auto commit() -> error_c override {
        int ret = avahi_entry_group_commit(_group);
        if (!ret) return error_c();
        return avahi_code(ret,"avahi_entry_group_commit");
    }
    auto state() -> int override {
        if (_group==nullptr) return 0;
        return avahi_entry_group_get_state(_group);
    }

    auto is_empty() -> int override {
        if (_group==nullptr) return 0;
        return avahi_entry_group_is_empty(_group);
    }
    auto add_service(CAvahiService item,
                    uint16_t port,
                    std::string subtype = "",
                    std::initializer_list<std::pair<std::string,std::string>> txt = {},
                    std::string host = std::string(),
                    AvahiPublishFlags flags = (AvahiPublishFlags) 0
                    ) -> error_c override {
        if (!_group) return avahi_code(-1,"add service without group");
        AvahiStringList *a = nullptr;
        for(auto& rec : txt) {
            if (rec.second.empty()) {
                a = avahi_string_list_add(a, rec.first.c_str());    
            } else {
                std::string trec = rec.first;
                trec.append("=");
                trec.append(rec.second);
                a = avahi_string_list_add(a, trec.c_str());
            }
        }
        int ret = avahi_entry_group_add_service_strlst(_group,item.interface,item.protocol,flags,item.name.c_str(),item.type.c_str(),item.domain.c_str(),host.c_str(),port,a);
        if (ret!=AVAHI_OK) return avahi_code(ret,"avahi_entry_group_add_service");
        if (!subtype.empty()) {
            ret = avahi_entry_group_add_service_subtype(_group,item.interface,item.protocol,flags,item.name.c_str(),item.type.c_str(),item.domain.c_str(),subtype.c_str());
            if (ret!=AVAHI_OK) return avahi_code(ret,"avahi_entry_group_add_service_subtype");
        }
        return error_c();
    }

    auto host_name() -> std::string  override {
        return _client->host_name();
    }
    
    static void callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
        auto* _this = (AvahiGroupImpl*)userdata;
        switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            if (_this->_on_established) _this->_on_established(_this);
            break;
        case AVAHI_ENTRY_GROUP_COLLISION : {
            if (_this->_on_collision) _this->_on_collision(_this);
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE :
            if (_this->_on_failure) {
                _this->_on_failure(avahi_code(avahi_client_errno(avahi_entry_group_get_client(g)),"avahi_entry_group failure"));
            }
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            break;
        }
    }

    CAvahiClient* _client;
    AvahiEntryGroup* _group = nullptr;
    OnEvent _on_create;
    OnEvent _on_collision;
    OnEvent _on_established;
    OnFailure _on_failure;
};

std::forward_list<AvahiGroupImpl*> AvahiGroupImpl::queries;

class AvahiHandlerImpl : public AvahiHandler {
public:
    auto init(const AvahiPoll* poll, AvahiClientFlags flags = (AvahiClientFlags)0) -> error_c {
        _client.on_failure([this](CAvahiClient* client) {
            log::debug()<<"AvahiClient on failure"<<std::endl;
            auto c = client->error("avahi client");
            for (auto query : AvahiQueryImpl::queries) {
                query->on_client_failure(c);
            }
            for (auto group : AvahiGroupImpl::queries) {
                group->on_client_failure(c);
            }
        });
        _client.on_state_changed([this](CAvahiClient* client, AvahiClientState state) {
            log::debug()<<"AvahiClient on state changed "<<state<<std::endl;
            for (auto group : AvahiGroupImpl::queries) {
                log::debug()<<"Group call"<<std::endl;
                switch (state) {
                case AVAHI_CLIENT_S_RUNNING: group->client_create(); break;
                case AVAHI_CLIENT_S_COLLISION:
                case AVAHI_CLIENT_S_REGISTERING: group->client_reset(); break;
                case AVAHI_CLIENT_CONNECTING:
                case AVAHI_CLIENT_FAILURE: ;
                }
            }
        });
        return _client.init(poll,flags);
    }
    void free() { _client.free();
    }
    auto query_service(CAvahiService pattern, AvahiLookupFlags flags) -> std::unique_ptr<AvahiQuery> override {
        return std::unique_ptr<AvahiQuery>(new AvahiQueryImpl{&_client,pattern,flags});
    }
    auto get_register_group() -> std::unique_ptr<AvahiGroup> override {
        return std::make_unique<AvahiGroupImpl>(&_client);
    }
    
    CAvahiClient _client;
};

auto AvahiHandler::create(const AvahiPoll* poll, AvahiClientFlags flags) -> std::unique_ptr<AvahiHandler> {
    auto* obj = new AvahiHandlerImpl{};
    obj->init(poll,flags);
    return std::unique_ptr<AvahiHandler>(obj);
}