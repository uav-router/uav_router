#include "log.h"
#include "loop.h"

void browser() {
    auto loop = IOLoopSvc::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto sb = loop->zeroconf()->query_service(CAvahiService("(.*)","_http._tcp").ipv4().itf("tap0"));
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
    loop->run();
}

void register_service() {
    auto loop = IOLoopSvc::loop();
    error_c ec = loop->handle_CtrlC();
    if (ec) {
        std::cout<<"Ctrl-C handler error "<<ec<<std::endl;
        return;
    }
    auto group = loop->zeroconf()->get_register_group();
    group->on_create([](AvahiGroup* g){ 
        error_c ec = g->add_service(
            CAvahiService("MegaPrn","_ipp._tcp").ipv4().itf("tap0"),
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
    loop->run();
}


int main() {
    Log::init();
    //Log::set_level(Log::Level::DEBUG,{"ioloop"});
    browser();
    //register_service();
    return 0;
}