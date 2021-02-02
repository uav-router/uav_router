#include "avahi-common.h"
#include <avahi-common/error.h>

CAvahiSimplePoll::CAvahiSimplePoll() {
    _poll = avahi_simple_poll_new();
}
CAvahiSimplePoll::~CAvahiSimplePoll() {
    if (_poll) avahi_simple_poll_free(_poll);
}
const AvahiPoll* CAvahiSimplePoll::get() {
    if (!_poll) return nullptr;
    return avahi_simple_poll_get(_poll);
}

int CAvahiSimplePoll::loop() {
    if (!_poll) return -1;
    return avahi_simple_poll_loop(_poll);
}

void CAvahiSimplePoll::quit() {
    if (!_poll) return;
    avahi_simple_poll_quit(_poll);
}

class TimeoutData: public CAvahiTimeout {
public:
    TimeoutData(CAvahiPoll::TimeoutFunc func, const AvahiPoll* poll):_func(func),_poll(poll) {}
    void update(const struct timeval *tv) override {
        _poll->timeout_update(_t,tv);
    }
    void free() override {
        _poll->timeout_free(_t);
        delete this;
    }
    void call() { _func(this); }
    
    AvahiTimeout* _t = nullptr;
    CAvahiPoll::TimeoutFunc _func;
    const AvahiPoll* _poll;
};

static void timeout_callback(AVAHI_GCC_UNUSED AvahiTimeout *e, void *_userdata) {
    static_cast<TimeoutData*>(_userdata)->call();
}

void CAvahiSimplePoll::on_timeout(const struct timeval *tv, TimeoutFunc func) {
    if (!_poll) return;
    const AvahiPoll* _p = avahi_simple_poll_get(_poll);
    auto data = new TimeoutData{func,_p};
    data->_t = _p->timeout_new(_p, tv, timeout_callback, data);
}

int CAvahiClient::init(const AvahiPoll* poll, AvahiClientFlags flags) {
    int ret=0;
    _client = avahi_client_new(poll, flags, callback, this, &ret);
    return ret;
}
void CAvahiClient::on_state_changed(OnStateChanged func) {
    _on_state = func;
}
void CAvahiClient::on_failure(OnFailure func) {
    _on_failure = func;
}
CAvahiClient::~CAvahiClient() {
    if (_client) {
        avahi_client_free(_client);
    }
}
const char* CAvahiClient::error() {
    return avahi_strerror(avahi_client_errno(_client));
}
AvahiClient * CAvahiClient::get() {
    return _client;
}
AvahiClientState CAvahiClient::state() {
    return avahi_client_get_state(_client);
}

void CAvahiClient::callback(AvahiClient *s, AvahiClientState state, void* userdata ) {
    CAvahiClient* _this = (CAvahiClient*)userdata;
    if (_this->_client==nullptr) _this->_client = s;
    if (state==AVAHI_CLIENT_FAILURE) {
        if (_this->_on_failure) { _this->_on_failure(_this);
        }
        return;
    }
    if (_this->_on_state) { _this->_on_state(_this, state);
    }
}

int CAvahiServiceBrowser::init(const char *type, AvahiClient* client, 
                                CAvahiService item, AvahiLookupFlags flags) {
    int ret=0;
    _sb = avahi_service_browser_new(client, item.interface, item.protocol, type, item.domain,
                                    flags, callback, this);
    return ret;
}
void CAvahiServiceBrowser::on_new(OnLookup func) { _on_new = func; }
void CAvahiServiceBrowser::on_remove(OnLookup func) {_on_remove = func;}
void CAvahiServiceBrowser::on_failure(OnEvent func) {_on_failure = func;}
void CAvahiServiceBrowser::on_all_for_now(OnEvent func) {_on_all_for_now = func;}
void CAvahiServiceBrowser::on_cache_exhausted(OnEvent func) {_on_cache_exhausted = func;}
CAvahiServiceBrowser::~CAvahiServiceBrowser() {
    if (_sb) {
        avahi_service_browser_free(_sb);
    }
}
const char* CAvahiServiceBrowser::error() {
    return avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(_sb)));
}
AvahiClient * CAvahiServiceBrowser::client() {
    return avahi_service_browser_get_client(_sb);
}
AvahiServiceBrowser * CAvahiServiceBrowser::get() {
    return _sb;
}
void CAvahiServiceBrowser::callback(AvahiServiceBrowser *b, AvahiIfIndex interface,
                     AvahiProtocol protocol, AvahiBrowserEvent event,
                     const char *name,       const char *type,
                     const char *domain,     AvahiLookupResultFlags flags,
                     void *userdata ) {
    CAvahiServiceBrowser* _this = (CAvahiServiceBrowser*)userdata;
    switch(event) {
        case AVAHI_BROWSER_NEW: {
            if (_this->_on_new) _this->_on_new(_this, CAvahiService{name,type,domain,interface,protocol},flags);
        } break;
        case AVAHI_BROWSER_REMOVE: {
            if (_this->_on_remove) _this->_on_remove(_this, CAvahiService{name,type,domain,interface,protocol},flags);
        } break;
        case AVAHI_BROWSER_CACHE_EXHAUSTED: {
            if (_this->_on_cache_exhausted) _this->_on_cache_exhausted(_this);
        } break;
        case AVAHI_BROWSER_ALL_FOR_NOW: {
            if (_this->_on_all_for_now) _this->_on_all_for_now(_this);
        } break;
        case AVAHI_BROWSER_FAILURE: {
            if (_this->_on_failure) _this->_on_failure(_this);
        }
    }
}

void CAvahiServiceResolver::on_resolve(OnResolve func) { _on_resolve=func;}
void CAvahiServiceResolver::on_failure(OnResolve func) { _on_failure=func;}
bool CAvahiServiceResolver::init(AvahiClient * client, CAvahiService item, 
        AvahiProtocol aprotocol, AvahiLookupFlags flags) {
    int ret=0;
    _sr = avahi_service_resolver_new(client, item.interface, item.protocol, item.name,
                                         item.type, item.domain, aprotocol, flags, callback, this);
    return _sr!=nullptr;
}
CAvahiServiceResolver::~CAvahiServiceResolver() {
    free();
}
void CAvahiServiceResolver::free() {
    if (_sr) {
        avahi_service_resolver_free(_sr);
        _sr = nullptr;
    }
}
const char* CAvahiServiceResolver::error() {
    return avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(_sr)));
}
AvahiServiceResolver * CAvahiServiceResolver::get() {
    return _sr;
}
void CAvahiServiceResolver::callback(AvahiServiceResolver *r,
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
    CAvahiServiceResolver* _this = (CAvahiServiceResolver*)userdata;
    OnResolve func;    
    if (event==AVAHI_RESOLVER_FAILURE) { func = _this->_on_failure;
    } else if (event==AVAHI_RESOLVER_FOUND) { func = _this->_on_resolve;
    } else {
        //TODO: unknown func
    }
    if (func) { 
        func(_this, CAvahiService{name, type, domain, interface, protocol}, 
           host_name, a, port, txt, flags
        );
    }
    //_this->free();
}

void CAvahiEntryGroup::init(AvahiClient * client) {
    _group = avahi_entry_group_new(client, callback, this);
}
void CAvahiEntryGroup::on_uncommited(OnEvent func) { _on_uncommited=func; }
void CAvahiEntryGroup::on_registering(OnEvent func) { _on_registering=func; }
void CAvahiEntryGroup::on_established(OnEvent func) { _on_established=func; }
void CAvahiEntryGroup::on_collision(OnEvent func) { _on_collision=func; }
void CAvahiEntryGroup::on_failure(OnEvent func) { _on_failure=func; }
const char* CAvahiEntryGroup::error() {
    if (!_group) return nullptr;
    return avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(_group)));
}
bool CAvahiEntryGroup::is_empty() {
    if (!_group) return true;
    return avahi_entry_group_is_empty(_group);
}

bool CAvahiEntryGroup::is_init() {
    return _group;
}

int CAvahiEntryGroup::add_service(CAvahiService item,
                    uint16_t port,
                    const char *host,
                    AvahiPublishFlags flags
                    ) {
    if (!_group) return -1;
    return avahi_entry_group_add_service_strlst(_group,item.interface,item.protocol,flags,item.name,item.type,item.domain,host,port,nullptr);
}
int CAvahiEntryGroup::add_service(CAvahiService item,
                    uint16_t port,
                    std::initializer_list<std::string> txt,
                    const char *host,
                    AvahiPublishFlags flags
                    ) {
    if (!_group) return -1;
    AvahiStringList *a = nullptr;
    for(auto& rec : txt) {
        a = avahi_string_list_add(a, rec.c_str());
    }
    return avahi_entry_group_add_service_strlst(_group,item.interface,item.protocol,flags,item.name,item.type,item.domain,host,port,a);
}

int CAvahiEntryGroup::add_service_subtype(CAvahiService item,
                            const char *subtype,
                            AvahiPublishFlags flags) {
    if (!_group) return -1;
    return avahi_entry_group_add_service_subtype(_group,item.interface,item.protocol,flags,item.name,item.type,item.domain,subtype);
}
int CAvahiEntryGroup::commit() {
    if (!_group) return -1;
    return avahi_entry_group_commit(_group);
}
int CAvahiEntryGroup::reset() {
    if (!_group) return -1;
    return avahi_entry_group_reset(_group);
}

void CAvahiEntryGroup::callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *_userdata) {
    CAvahiEntryGroup *_this = (CAvahiEntryGroup *)_userdata;
    _this->_group = g;
    /* Called whenever the entry group state changes */
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            if (_this->_on_established) _this->_on_established(_this);
            break;
        case AVAHI_ENTRY_GROUP_COLLISION : {
            if (_this->_on_collision) _this->_on_collision(_this);
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE :
            if (_this->_on_failure) _this->_on_failure(_this);
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
            if (_this->_on_uncommited) _this->_on_uncommited(_this);
            break;
        case AVAHI_ENTRY_GROUP_REGISTERING:
            if (_this->_on_registering) _this->_on_registering(_this);
            break;
    }
}