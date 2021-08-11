#include <exception>
#include <ioloop.h>
#include <filters.h>
#include <log.h>
#include <err.h>
#include <memory>
#include <set>
#include <string>
#include <regex>
#include <tuple>
#include <utility>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <chrono>
using namespace std::chrono_literals;


#ifdef USING_SENTRY
#include <sentry.h>
#endif


Log::Log rlog {"router"};

// Collection of endpoints to write the same stream of data
class Destination : public Writeable {
public:
    void add(const std::shared_ptr<Writeable> endpoint) {
        endpoints[endpoint.get()] = endpoint;
    }
    void clear() {
        endpoints.clear();
    }
    auto write(const void* buf, int len) -> int override {
        if (endpoints.empty()) return 0;
        if (endpoints.size()==1) {
            if (endpoints.cbegin()->second.expired()) {
                endpoints.clear();
                return 0;
            }
            return endpoints.cbegin()->second.lock()->write(buf,len);
        }
        for(auto endpoint = endpoints.cbegin(); endpoint != endpoints.cend();) {
            if (endpoint->second.expired()) {
                endpoint = endpoints.erase(endpoint);
            } else {
                endpoint->second.lock()->write(buf,len);
                //TODO: partial writes
                ++endpoint;
            }
        }
        return len;
    }
    bool empty() { return endpoints.empty();
    }
private:
    std::map<Writeable*,std::weak_ptr<Writeable>> endpoints;
};

class EndpointStore {
public:
    // add regex or endpoint name to table of endpoints
    void register_name(const std::string& name) {
        auto& d = endpoints[name] = std::make_shared<Destination>();
        if (name[0]=='/') {
            regex_endpoints.emplace_back(std::make_pair(std::regex(name.substr(1)),d));
        }
    }

    void register_write_end(const std::string& name, std::shared_ptr<Writeable> sink) {
        auto& endpoint = endpoints[name];
        if (endpoint) { endpoint->add(sink);
        }
        std::smatch match;
        for(auto& entry : regex_endpoints) {
            if (std::regex_match(name,match,entry.first)) {
                entry.second->add(sink);
            }
        }
    }

    void connect_to_dest(const std::string& name, std::shared_ptr<Destination>& dest) {
        auto ptr = endpoints.find(name);
        if (ptr!=endpoints.end()) {
            dest->add(ptr->second);
        }
        if (name[0]=='/') return;
        std::smatch match;
        for(auto& entry : regex_endpoints) {
            if (std::regex_match(name,match,entry.first)) {
                dest->add(entry.second);
            }
        }
    }

    void clear() {
        endpoints.clear();
        regex_endpoints.clear();
    }

private:
    std::map<std::string,std::shared_ptr<Destination>> endpoints;
    std::vector<std::pair<std::regex,std::shared_ptr<Destination>>> regex_endpoints;
};

EndpointStore endpoint_store;

std::vector<std::tuple<std::string,std::string,YAML::Node>> routes;

bool construct_route(YAML::Node cfg, std::shared_ptr<Destination>& dest, std::vector<std::shared_ptr<Filter>>& filters) {
    if (cfg.IsScalar())  {
        endpoint_store.connect_to_dest(cfg.as<std::string>(),dest);
        return true;
    }
    if (cfg.IsSequence()) {
        for(auto d : cfg) {
            construct_route(d,dest,filters);
        }
        return true;
    }
    if (!cfg.IsMap()) { 
        return false;
    }
    auto type = cfg["type"];
    if (!type) {
        rlog.error()<<"Unknown filter type"<<std::endl;
        return false;
    }
    auto filter = Filters::create(type.as<std::string>(),cfg);
    if (!filter) {
        rlog.error()<<"Create filter type "<<type.as<std::string>()<<" failed."<<std::endl;
        return false;
    }
    filters.push_back(filter);
    auto dst = cfg["dst"];
    if (dst) {
        auto next = std::make_shared<Destination>();
        filter->chain(next);
        construct_route(dst,next,filters);
    }
    auto rest = cfg["rest"];
    if (rest) {
        auto rst = std::make_shared<Destination>();
        filter->rest(rst);
        construct_route(rest,rst,filters);
    }
    dest->add(filter);
    return true;
}

bool equal_name(const std::string& pattern, const std::string& name) {
    if (pattern[0]=='/') {
        std::regex rg(pattern.substr(1));
        std::smatch match;
        return std::regex_match(name,match,rg);
    }
    return pattern==name;
}

void construct_routes(const std::string& name, std::shared_ptr<Destination>& dest, std::vector<std::shared_ptr<Filter>>& filters) {
    for(auto& route: routes) {
        auto [endpoint_name, route_name, dst] = route;
        if (equal_name(endpoint_name, name)) {
            construct_route(dst, dest, filters);
        }
    }
}

/*
Fill named_endpoints & regex_endpoints
*/
bool scan_dst(YAML::Node cfg) {
    if (!cfg) return false;
    if (cfg.IsScalar()) {
        endpoint_store.register_name(cfg.as<std::string>());
        return true;
    }
    if (cfg.IsSequence()) {
        for(auto name : cfg) {
            if (!scan_dst(name)) return false;
        }
        return true;
    }
    if (cfg.IsMap()) {
        auto dst = cfg["dst"];
        auto rest = cfg["rest"];
        if (!dst && !rest) { return false;
        }
        if (dst) {
            if (!scan_dst(dst)) return false;
        }
        if (rest) {
            if (!scan_dst(rest)) return false;
        }
        return true;
    }
    return false;
}

/*
Fill routes, named_endpoints & regex_endpoints
*/
bool load_routes(YAML::Node cfg) {
    if (!cfg) {
        Log::error()<<"No routes section described"<<std::endl;
        return false;
    }
    if (!cfg.IsMap()) {
        Log::error()<<"Routes section is not a collection of routes"<<std::endl;
        return false;
    }
    for (auto route: cfg) {
        if (!route.second.IsMap()) {
            Log::error()<<"Route "<<route.first.as<std::string>()<<" has wrong format"<<std::endl;
            continue;
        }
        auto dst = route.second["dst"];
        if (!scan_dst(dst)) {
            Log::error()<<"Route "<<route.first.as<std::string>()<<" has wrong dst"<<std::endl;
            continue;
        }
        
        auto src = route.second["src"];
        if (!src) {
            Log::error()<<"Route "<<route.first.as<std::string>()<<" has no src"<<std::endl;
            continue;
        }
        if (src.IsScalar()) {
            routes.emplace_back(std::make_tuple(src.as<std::string>(), route.first.as<std::string>(),dst));
            continue;
        }
        if (src.IsSequence()) {
            for(auto name : src) {
                routes.emplace_back(std::make_tuple(name.as<std::string>(), route.first.as<std::string>(),dst));
            }
        }
    }
    return true;
}

struct SourceEntry {
    std::shared_ptr<StreamSource> connection;
    std::shared_ptr<Destination> destination;
    std::vector<std::shared_ptr<Filter>> filters;
    SourceEntry() : destination(std::make_shared<Destination>()) {}
};

struct ClientEntry {
    std::shared_ptr<Client> client;
    std::shared_ptr<Destination> destination;
    std::vector<std::shared_ptr<Filter>> filters;
    ClientEntry() : destination(std::make_shared<Destination>()) {}
};

std::map<std::string, SourceEntry> source_entries;
std::map<std::string, ClientEntry> client_entries;
std::vector<std::shared_ptr<OFileStream>> file_entries;

void setup_endpoint(const std::string& name, std::shared_ptr<StreamSource> endpoint, bool register_write_end = true) {
    auto& entry = source_entries[name];
    entry.connection = std::move(endpoint);
    construct_routes(name,entry.destination,entry.filters);
    entry.connection->on_error([name](const error_c& ec) {
        rlog.error()<<"Endpoint ["<<name<<"]:"<<ec<<std::endl;
    });
    entry.connection->on_connect([&entry, name, register_write_end](std::shared_ptr<Client> cli, std::string cli_name){
        if (register_write_end) {
            endpoint_store.register_write_end(cli_name,cli);
            endpoint_store.register_write_end(name,cli);
        }
        auto& client = client_entries[cli_name];
        client.client = cli;
        client.destination->clear();
        construct_routes(cli_name,client.destination,client.filters);
        client.destination->add(entry.destination);
        cli->on_close([cli_name](){
            client_entries.erase(cli_name);
        });
        cli->on_read([&client](void* buf, int len){
            client.destination->write(buf,len);
        });
        cli->on_error([&entry, cli_name](error_c& ec) {
            entry.connection->on_error(ec,cli_name);
        });
    });
}

bool load_endpoints(std::unique_ptr<IOLoop>& loop, YAML::Node cfg) {
    if (!cfg) return false;
    if (!cfg.IsMap()) return false;
    enum EndpointType { UART, TCPSVR, TCPCLI, UDPSVR};
    std::vector<std::pair<EndpointType,YAML::Node>> data;
    if (!cfg) return false;
    if (!cfg.IsMap()) return false;
    auto uart = cfg["uart"];
    if (uart.IsMap()) {
        data.push_back(std::make_pair(EndpointType::UART, uart));
    }
    auto tcp = cfg["tcp"];
    if (tcp.IsMap()) {
        auto clients = tcp["clients"];
        if (clients.IsMap()) {
            data.push_back(std::make_pair(EndpointType::TCPCLI, clients));
        }
        auto servers = tcp["servers"];
        if (servers.IsMap()) {
            data.push_back(std::make_pair(EndpointType::TCPSVR, servers));
        }
    }
    auto udp = cfg["udp"];
    if (udp.IsMap()) {
        auto clients = udp["clients"];
        if (clients.IsMap()) {
            // create udp clients
            for(auto client : clients) {
                auto name = client.first.as<std::string>();
                try {
                    auto endpoint = loop->udp_client(name);
                    if (endpoint) {
                        error_c ret = endpoint->init_yaml(client.second);
                        if (ret) { rlog.error()<<"Init udp client endpoint "<<name<<" error "<<ret<<std::endl;
                        } else { 
                            std::shared_ptr<UdpClient> c = std::move(endpoint);
                            endpoint_store.register_write_end(name,c);
                            setup_endpoint(name,c,false);
                        }
                    }
                } catch(std::exception &e) {
                    rlog.error()<<"Exception while construct udp client "<<name<<" "<<e.what()<<std::endl;
                }
            }
        }
        auto servers = udp["servers"];
        if (servers.IsMap()) {
            data.push_back(std::make_pair(EndpointType::UDPSVR, servers));
        }
    }
    for (auto& item : data) {
        for(auto endp : item.second) {
            auto name = endp.first.as<std::string>();
            try {
                std::unique_ptr<StreamSource> endpoint;
                switch(item.first) {
                case UART: endpoint = loop->uart(name); break;
                case TCPSVR: endpoint = loop->tcp_server(name); break;
                case TCPCLI: endpoint = loop->tcp_client(name); break;
                case UDPSVR: endpoint = loop->udp_server(name); break;
                }
                if (endpoint) {
                    error_c ret = endpoint->init_yaml(endp.second);
                    if (ret) { rlog.error()<<"Init endpoint "<<name<<" error "<<ret<<std::endl;
                    } else {   setup_endpoint(name,std::move(endpoint));
                    }
                }
            } catch(std::exception &e) {
                rlog.error()<<"Exception while construct uart "<<name<<" "<<e.what()<<std::endl;
            }
        }
    }
    auto files = cfg["file"];
    if (files.IsMap()) {
        //create files
        for(auto file : files) {
            if (file.second.IsMap()) {
                auto f = loop->outfile();
                error_c ret = f->init_yaml(file.second);
                auto name = file.first.as<std::string>();
                if (ret)  {
                    rlog.error()<<"Init file endpoint "<<name<<" error "<<ret<<std::endl;
                } else {
                    auto& of = file_entries.emplace_back(std::move(f));
                    endpoint_store.register_write_end(name,of);
                }
            }
        }
    }
    return true;
}

void cleanup() {
    endpoint_store.clear();
    routes.clear();
    source_entries.clear();
    client_entries.clear();
    file_entries.clear();
}

void load_loggers(YAML::Node cfg) {
    std::vector<std::pair<std::string, Log::Level>> levels = {
        {"disable", Log::Level::DISABLE},
        {"error", Log::Level::ERROR},
        {"warning", Log::Level::WARNING},
        {"notice", Log::Level::NOTICE},
        {"info", Log::Level::INFO},
        {"debug", Log::Level::DEBUG}
    };
    if (cfg && cfg.IsMap()) {
        for(auto& item : levels) {
            auto chapter = cfg[item.first];
            if (!chapter) continue;
            if (chapter.IsScalar()) {
                Log::set_level(item.second, {chapter.as<std::string>()});
            } else if (chapter.IsSequence()) {
                for(auto entry : chapter) {
                    Log::set_level(item.second, {entry.as<std::string>()});
                }
            }
        }
    }
}

std::string read_file(const std::string& name) {
    struct stat sb{};
    std::string res;
    int fd = open(name.c_str(), O_RDONLY);
    if (fd < 0) {
        errno_c err;
        std::cerr<<"Error opening "<<name<<". "<<err<<std::endl;
        return std::string();
    }
    fstat(fd, &sb);
    res.resize(sb.st_size);
    read(fd, (char*)(res.data()), sb.st_size);
    close(fd);
    return std::move(res);
}

std::string get_var(const std::string& name, const std::string& def) {
    auto ret = getenv(name.c_str());
    if (ret) return ret;
    return def;
}

std::string expand_shell_variables(std::string data) {
    if (data.empty()) return data;
    static const std::regex ENV{"\\$\\{([^}\\:]+)(\\:\\-([^}]+))?\\}"};
    std::smatch match;
    while (std::regex_search(data, match, ENV)) { 
        data.replace(match.begin()->first, match[0].second, get_var(match[1].str(),match[3].str()));
    }
    return std::move(data);
}

#ifdef USING_SENTRY
static void
print_envelope(sentry_envelope_t *envelope, void *unused_state)
{
    (void)unused_state;
    size_t size_out = 0;
    char *s = sentry_envelope_serialize(envelope, &size_out);
    printf("%s", s);
    sentry_free(s);
    sentry_envelope_free(envelope);
}
class Sentry {
public:
    void init(YAML::Node cfg) {
        sentry_options_t *options = sentry_options_new();
        sentry_options_set_dsn(options, "https://657e42f753e64d4591e29f034782803d@o884048.ingest.sentry.io/5837073");
        sentry_options_set_release(options, GIT_COMMIT);
        //sentry_options_set_transport(options, sentry_transport_new(print_envelope));
        sentry_init(options);
        _initialized = true;
    }
    ~Sentry() {
        if (_initialized) {
            sentry_close();
        }
    }
private:
    bool _initialized = false;
};
#endif

int main(int argc, char *argv[]) {
#ifdef USING_SENTRY
    Sentry sentry;
#endif
    std::unique_ptr<Timer> timer;
    Log::init();
    Log::set_level(Log::Level::DEBUG,{"router"});
    auto loop = IOLoop::loop();
    std::string config_file_name = "config.yaml";
    if (argc>1) {
        config_file_name = argv[1];
    }
    std::cerr<<"Load config file "<<config_file_name<<std::endl;
    std::string expanded = std::move(expand_shell_variables(read_file(config_file_name)));
    if (expanded.empty()) return 1;
    YAML::Node config = YAML::Load(expanded);
    auto global_cfg = config["config"];
    if (global_cfg && global_cfg.IsMap()) {
#ifdef USING_SENTRY
        sentry.init(global_cfg["sentry"]);
#endif        
        auto test = global_cfg["test"];
        if (test && test.IsScalar()) {
            auto test_name = test.as<std::string>();
            std::cout<<"Test "<<test_name<<std::endl;
            if (test_name == "divzero") {
                std::cout<<"Start divzero test"<<std::endl;
                timer = loop->timer();
                int var = 0;
                timer->shoot([&var](){
                    std::cout<<"Do divzero"<<std::endl;
                    var = 10/var;
                    var+=2;
                    std::cout<<"After divzero"<<std::endl;
                }).arm_oneshoot(5s);
            } else if (test_name == "segfault") {
                std::cout<<"Start segfault test"<<std::endl;
                timer = loop->timer();
                int var = 0;
                timer->shoot([&var](){
                    std::cout<<"Do segfault"<<std::endl;
                    *(int*)0=0;
                    std::cout<<"After segfault"<<std::endl;
                }).arm_oneshoot(5s);
            }
        }
        bool ctrlC = true;
        auto ctrlC_cfg = global_cfg["ctrl-C"];
        if (ctrlC_cfg && ctrlC_cfg.IsScalar()) {
            ctrlC = ctrlC_cfg.as<bool>();
        }
        if (ctrlC) {
            error_c ec = loop->handle_CtrlC();
            if (ec) {
                std::cerr<<"Ctrl-C handler error "<<ec<<std::endl;
                return 1;
            }
        }
    }
    load_loggers(config["logging"]);
    if (!load_routes(config["routes"])) return 2;
    loop->zeroconf_ready([&loop,endpoints = config["endpoints"]](){
        if (!load_endpoints(loop, endpoints)) {
            std::cerr<<"Error creating endpoints. Stop."<<std::endl;
            loop->stop();
        }    
    });
    auto stat_cfg = config["stats"];
    if (stat_cfg && stat_cfg.IsMap()) {
        auto endpoint = stat_cfg["endpoint"];
        if (endpoint && endpoint.IsScalar()) {
            auto output = std::make_shared<Destination>();
            endpoint_store.connect_to_dest(endpoint.as<std::string>(),output);
            if (!output->empty()) {
                auto stats = loop->stats();
                stats->init_yaml(output, stat_cfg);
            }
        }
    }
    loop->run();
    cleanup();
    return 0;
}