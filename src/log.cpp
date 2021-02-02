#include <unistd.h>
#include <iostream>
#include <avahi-core/log.h>
#include "log.h"

namespace log {
    class NullBuffer : public std::streambuf {
    public:
      auto overflow(int c) -> int final { return c; }
    };
    NullBuffer null_buffer;
    std::ostream null_stream(&null_buffer);

    static Level max_level = Level::INFO;
    static bool use_color = false;

    void set_level(Level level) {
        max_level = level;
    }

    auto log(Level level) -> std::ostream& {
        if (max_level<level) return null_stream;
        if (use_color) {
            switch (level) {
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

    void avahiLogFunction(AvahiLogLevel level, const char *txt) {
        Level l=Level::DEBUG;
        switch(level) {
        case AVAHI_LOG_ERROR : l = Level::ERROR; break;
        case AVAHI_LOG_WARN  : l = Level::WARNING; break;
        case AVAHI_LOG_NOTICE: l = Level::NOTICE; break;
        case AVAHI_LOG_INFO  : l = Level::INFO; break;
        case AVAHI_LOG_DEBUG : l = Level::DEBUG; break;
        case AVAHI_LOG_LEVEL_MAX: break;
        }
        log(l)<<txt<<std::endl;
    }

    void init() {
        use_color = isatty(STDERR_FILENO);
        avahi_set_log_function(avahiLogFunction);
    }

    auto debug() -> std::ostream&   { return log(Level::DEBUG);  }
    auto info() -> std::ostream&    { return log(Level::INFO);   }
    auto notice() -> std::ostream&  { return log(Level::NOTICE); }
    auto warning() -> std::ostream& { return log(Level::WARNING);}
    auto error() -> std::ostream&   { return log(Level::ERROR);  }
}