#include <net/if.h>
#include <avahi-common/error.h>
#include <log.h>
#include <err.h>
#include <loop.h>
#include <timer.h>

void print_addr(const struct sockaddr *sa, socklen_t salen) {
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    eai_code ec = getnameinfo(sa, salen, host, sizeof(host), port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);
    if (ec) {
       std::cout<<"Get nameinfo error: "<<ec.message()<<" family="<<sa->sa_family<<" len="<<salen<<std::endl;
    } else {
        std::cout<<"address = "<<host<<":"<<port<<std::endl;
    }
}


void avahi_browser() {
    IOLoop loop;

    CAvahiService pattern;
    pattern.type = "_ipp._tcp";
    //pattern.protocol = AVAHI_PROTO_INET;
    //pattern.interface = if_nametoindex("tap0");
    auto sb = loop.query_service(pattern,(AvahiLookupFlags)0);
    sb->on_failure([](error_c ec){
        std::cout<<"Query service error: "<<ec<<std::endl;
    });
    sb->on_complete([](){
        std::cout<<"=============================="<<std::endl;
    });
    sb->on_remove([](CAvahiService service, AvahiLookupResultFlags flags){
        std::cout<<"REMOVE: service "<< service.name<<" of type "<<service.type<<" in domain '"<<service.domain<<"'"<<std::endl;
    });
    sb->on_resolve([](CAvahiService service, std::string host_name,
        const sockaddr_storage& addr, std::vector<std::pair<std::string,std::string>> txt,
        AvahiLookupResultFlags flags){
        
        std::cout<<"ADD: service "<< service.name<<" of type "<<service.type<<" in domain '"<<service.domain<<"'"<<std::endl;    
        if (addr.ss_family==AF_INET) {
            print_addr((sockaddr*)&addr,sizeof(sockaddr_in));
        } else if (addr.ss_family==AF_INET6) {
            print_addr((sockaddr*)&addr,sizeof(sockaddr_in6));
        } else {
            std::cout<<"Unknown address family"<<std::endl;
        }
        for (auto& rec : txt) {
            if (rec.second.empty()) {
                std::cout<<rec.first<<std::endl;
            } else {
                std::cout<<rec.first<<"="<<rec.second<<std::endl;
            }
        }
        std::cout<<"is_local: "<<!!(flags & AVAHI_LOOKUP_RESULT_LOCAL);
        std::cout<<"\tour_own: "<<!!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN);
        std::cout<<"\twide_area: "<<!!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA);
        std::cout<<"\tmulticast: "<<!!(flags & AVAHI_LOOKUP_RESULT_MULTICAST);
        std::cout<<"\tcached: "<<!!(flags & AVAHI_LOOKUP_RESULT_CACHED)<<std::endl;
    });
    loop.run();
}

int main() {
    log::init();
    log::set_level(log::Level::DEBUG);
    avahi_browser();
    return 0;
}