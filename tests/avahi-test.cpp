#include <avahi-common/error.h>
#include <log.h>
#include <err.h>
#include <loop.h>
#include <netinet/in.h>
#include <timer.h>
#include <addrinfo.h>

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

void avahi_register() {
    IOLoop loop;
    auto group = loop.get_register_group();
    group->on_create([](AvahiGroup* g){ 
        error_c ec = g->add_service(
            CAvahiService("MegaPrn","_ipp._tcp").set_ipv4().set_interface("tap0"),
            651
        );
        if (ec) {
            std::cout<<"Error adding service: "<<ec<<std::endl;
            ec = g->reset();
            if (ec) std::cout<<"Error reset service: "<<ec<<std::endl;
        } else {
            ec = g->commit();
            if (ec) std::cout<<"Error commit service: "<<ec<<std::endl;
        }
    });
    group->on_collision([](AvahiGroup* g){ 
        std::cout<<"Group collision"<<std::endl;
        g->reset();
    });
    group->on_established([](AvahiGroup* g){ std::cout<<"Service registered"<<std::endl;
    });
    group->on_failure([](error_c ec){ std::cout<<"Group error"<<ec<<std::endl;
    });
    group->create();
    loop.run();
}

#include <stdio.h>
error_c get_port_number(addrinfo* ai, uint16_t &port, int &fd) {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd==-1) {
        errno_c ec("Error opening port allocating socket");
        return ec;
    }
    error_c ret;
    int yes = 1;
    ret = err_chk(setsockopt(fd,SOL_SOCKET,SO_REUSEPORT,&yes,sizeof(yes)),"reuseport");
    if (ret) {
        close(fd);
        return ret;
    }
    //SockAddr addr(INADDR_ANY,0);
    SockAddr addr(ai);
    std::cout<<"Bind addr "<<addr<<std::endl;
    ret = addr.bind(fd);
    if (ret) { 
        close(fd);
        return ret;
    }
    SockAddr out(fd);
    std::cout<<"Out addr "<<out<<std::endl;
    port = out.port();
    return error_c();
}

void auto_port() {
    IOLoop loop;
    std::string itf = "tap0";
    auto resolver = AddressResolver::create();
    resolver->on_resolve([&loop,&itf](addrinfo* ai){
        uint16_t port = 0;
        int fd = -1;
        error_c ec = get_port_number(ai,port,fd);
        if (ec) {
            std::cerr<<ec<<std::endl;
            return;
        }
        if (!port) {
            std::cerr<<"No port found"<<std::endl;
            return;
        }
        auto group = loop.get_register_group();
        group->on_create([&port,&fd,&itf](AvahiGroup* g){ 
            std::cout<<"Our host is "<<g->host_name()<<std::endl;
            error_c ec = g->add_service(
                CAvahiService("MegaPrn","_ipp._tcp").set_ipv4().set_interface(itf),
                port
            );
            if (ec) {
                std::cout<<"Error adding service: "<<ec<<std::endl;
                ec = g->reset();
                if (ec) std::cout<<"Error reset service: "<<ec<<std::endl;
                return;
            }
            std::string name = std::to_string(port)+"."+itf+"."+g->host_name()+".tcp";
            ec = g->add_service(
                CAvahiService(name,"_portclaim._tcp").set_ipv4().set_interface(itf),
                port
            );
            if (ec) {
                std::cout<<"Error adding claim service: "<<ec<<std::endl;
                ec = g->reset();
                if (ec) std::cout<<"Error reset service: "<<ec<<std::endl;
                return;
            }
            ec = g->commit();
            if (ec) std::cout<<"Error commit service: "<<ec<<std::endl;
            
        });
        group->on_collision([](AvahiGroup* g){ 
            std::cout<<"Group collision"<<std::endl;
            g->reset();
        });
        group->on_established([](AvahiGroup* g){ std::cout<<"Service registered"<<std::endl;
        });
        group->on_failure([](error_c ec){ std::cout<<"Group error"<<ec<<std::endl;
        });
        group->create();
    });
    resolver->local_interface(itf,0,&loop);
    
    loop.run();
}


void avahi_browser() {
    IOLoop loop;

    auto sb = loop.query_service(CAvahiService("anon(.*)","_ipp._tcp").set_ipv4().set_interface("tap0"));
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
        SockAddr addr, std::vector<std::pair<std::string,std::string>> txt,
        AvahiLookupResultFlags flags){
        
        std::cout<<"ADD: service "<< service.name<<" of type "<<service.type<<" in domain '"<<service.domain<<"'"<<std::endl;    
        std::cout<<addr<<std::endl;
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
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"libavahi","avahi"});
    avahi_browser();
    //avahi_register();
    //auto_port();
    return 0;
}