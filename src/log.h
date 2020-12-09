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

    extern std::ostream& debug();
    extern std::ostream& info();
    extern std::ostream& notice();
    extern std::ostream& warning();
    extern std::ostream& error();
};
#endif //__LOG_H__