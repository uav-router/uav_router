#include <unistd.h>
#include <iostream>
#include <map>
#include <utility>
#include <avahi-core/log.h>
#include "log.h"

namespace Log {
    class NullBuffer : public std::streambuf {
    public:
      auto overflow(int c) -> int final { return c; }
    };
    NullBuffer null_buffer;
    std::ostream null_stream(&null_buffer);
    static bool use_color = false;

    auto _log(Level level, Level max_level) -> std::ostream& {
        if (max_level<level) return null_stream;
        if (use_color) {
            switch (level) {
                case Level::DISABLE: break;
                case Level::ERROR:
                    std::cerr<<"\033[31m"; //RED
                    break;
                case Level::WARNING:
                    std::cerr<<"\033[0;33m"; //ORANGE
                    break;
                case Level::NOTICE:
                    std::cerr<<"033[33;1m"; //YELLOW
                    break;
                case Level::INFO:
                    std::cerr<<"\033[37;1m"; //WHITE
                    break;
                case Level::DEBUG:
                    std::cerr<<"\033[34;1m"; //LIGHTBLUE
            }
        }
        return std::cerr;
    }

    static std::map<std::string, Log*> loggers;

    Log::Log(std::string name):_name(std::move(name)) {
        loggers.emplace(_name,this);
    };
    Log::~Log() { loggers.erase(_name);
    }
        
    void Log::set_level(Level level) {max_level = level;}   
    auto Log::log(Level level) -> std::ostream& { return _log(Level::DEBUG, max_level); }

    void avahiLogFunction(AvahiLogLevel level, const char *txt) {
        static Log log("libavahi");
        Level l=Level::DEBUG;
        switch(level) {
        case AVAHI_LOG_ERROR : l = Level::ERROR; break;
        case AVAHI_LOG_WARN  : l = Level::WARNING; break;
        case AVAHI_LOG_NOTICE: l = Level::NOTICE; break;
        case AVAHI_LOG_INFO  : l = Level::INFO; break;
        case AVAHI_LOG_DEBUG : l = Level::DEBUG; break;
        case AVAHI_LOG_LEVEL_MAX: break;
        }
        log.log(l)<<txt<<std::endl;
    }

    void init() {
        use_color = isatty(STDERR_FILENO);
        avahi_set_log_function(avahiLogFunction);
    }

    Log l("default");

    auto debug() -> std::ostream&   { return l.log(Level::DEBUG);  }
    auto info() -> std::ostream&    { return l.log(Level::INFO);   }
    auto notice() -> std::ostream&  { return l.log(Level::NOTICE); }
    auto warning() -> std::ostream& { return l.log(Level::WARNING);}
    auto error() -> std::ostream&   { return l.log(Level::ERROR);  }

    void set_level(Level level, std::initializer_list<std::string> lognames) {
        if (lognames.size()==0) {
            l.set_level(level);
            return;
        }
        for(auto name : lognames) {
            if (name=="all") {
                for (auto& l : loggers) {
                    l.second->set_level(level);
                }
                return;
            }
            auto it = loggers.find(name);
            if (it==loggers.end()) continue;
            it->second->set_level(level);
        }
    }
}