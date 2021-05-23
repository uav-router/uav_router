//clang++ others/yaml.cpp -o bintest/yaml -I dependencies/yaml-cpp/include -std=c++11 -Ldependencies/yaml-cpp/build -lyaml-cpp

#include <yaml-cpp/yaml.h>
#include <cassert>
#include <iostream>

void handle_uart(std::string name, YAML::Node cfg) {
    std::cout<<"--- Uart "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_tcp_client(std::string name, YAML::Node cfg) {
    std::cout<<"--- Tcp client "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_udp_client(std::string name, YAML::Node cfg) {
    std::cout<<"--- Udp client "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_tcp_server(std::string name, YAML::Node cfg) {
    std::cout<<"--- Tcp server "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_udp_server(std::string name, YAML::Node cfg) {
    std::cout<<"--- Udp server "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_file(std::string name, YAML::Node cfg) {
    std::cout<<"--- File "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_uarts(YAML::Node cfg) {
    //for(YAML::const_iterator it=cfg.begin();it!=cfg.end();++it) {
    //    handle_uart(it->first.as<std::string>(),it->second);
    //}
    for(auto it: cfg) {
        handle_uart(it.first.as<std::string>(),it.second);
    }
}
void handle_tcps(YAML::Node cfg) {
    auto clients = cfg["clients"];
    if (clients && clients.IsMap()) {
        for(auto it: clients) {
            handle_tcp_client(it.first.as<std::string>(),it.second);
        }
    }
    auto servers = cfg["servers"];
    if (servers && servers.IsMap()) {
        for(auto it:servers) {
            handle_tcp_server(it.first.as<std::string>(),it.second);
        }
    }
}
void handle_udps(YAML::Node cfg) {
    auto clients = cfg["clients"];
    if (clients && clients.IsMap()) {
        for(auto it: clients) {
            handle_udp_client(it.first.as<std::string>(),it.second);
        }
    }
    auto servers = cfg["servers"];
    if (servers && servers.IsMap()) {
        for(auto it: servers) {
            handle_udp_server(it.first.as<std::string>(),it.second);
        }
    }
}
void handle_files(YAML::Node cfg) {
    for(auto it: cfg) {
        handle_file(it.first.as<std::string>(),it.second);
    }
}

void handle_endpoints(YAML::Node cfg) {
    std::cout<<"Endpoints:"<<std::endl;
    auto uarts = cfg["uart"];
    if (uarts && uarts.IsMap()) {
        handle_uarts(uarts);
    }
    auto tcps = cfg["tcp"];
    if (tcps && tcps.IsMap()) {
        handle_tcps(tcps);
    }
    auto udps = cfg["udp"];
    if (udps && udps.IsMap()) {
        handle_udps(udps);
    }
    auto files = cfg["file"];
    if (files && files.IsMap()) {
        handle_files(files);
    }
}

void handle_route(std::string name, YAML::Node cfg) {
    std::cout<<"--- Route "<<name<<" ---"<<std::endl;
    std::cout<<cfg<<std::endl;
}

void handle_routes(YAML::Node cfg) {
    std::cout<<"Routes:"<<std::endl;
    for(auto it: cfg) {
        handle_route(it.first.as<std::string>(),it.second);
    }
}

void handle_stats(YAML::Node cfg) {
    std::cout<<"=== Stats ==="<<std::endl;
    std::cout<<cfg<<std::endl;
}

int main() {
    YAML::Node config = YAML::LoadFile("others/config.yml");
    if (!config.IsMap()) {
        std::cerr<<"Config must be a map"<<std::endl;
        return 0;
    }
    auto endpoints = config["endpoints"];
    if (endpoints && endpoints.IsMap()) {
        handle_endpoints(endpoints);
    }
    auto routes = config["routes"];
    if (routes && routes.IsMap()) {
        handle_routes(routes);
    }
    auto stats = config["stats"];
    if (stats && stats.IsMap()) {
        handle_stats(stats);
    }
    return 0;
}