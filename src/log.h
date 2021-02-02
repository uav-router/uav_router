#ifndef __LOG_H__
#define __LOG_H__
#include <iostream>

namespace log {
    enum class Level {
        ERROR = 0,
        WARNING,
        NOTICE,
        INFO,
        DEBUG,
    };

    extern void init();
    extern void set_level(Level level);

    extern auto debug() -> std::ostream&;
    extern auto info() -> std::ostream&;
    extern auto notice() -> std::ostream&;
    extern auto warning() -> std::ostream&;
    extern auto error() -> std::ostream&;
};
#endif //__LOG_H__