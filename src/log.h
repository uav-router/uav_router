#ifndef __LOG_H__
#define __LOG_H__
#include <iostream>
#include <memory>
#include <initializer_list>

namespace Log {
    enum class Level {
        DISABLE = 0,
        ERROR, 
        WARNING,
        NOTICE,
        INFO,
        DEBUG,
    };

    extern void init();
    extern void set_level(Level level, std::initializer_list<std::string> lognames = {});

    extern auto debug() -> std::ostream&;
    extern auto info() -> std::ostream&;
    extern auto notice() -> std::ostream&;
    extern auto warning() -> std::ostream&;
    extern auto error() -> std::ostream&;

    class Log {
    public:
        Log(std::string name);
        ~Log();
        auto log(Level level) -> std::ostream&;
        void set_level(Level level);
        auto debug() -> std::ostream& { return log(Level::DEBUG);  }
        auto info() -> std::ostream& { return log(Level::INFO);  }
        auto notice() -> std::ostream& { return log(Level::NOTICE);  }
        auto warning() -> std::ostream& { return log(Level::WARNING);  }
        auto error() -> std::ostream& { return log(Level::ERROR);  }
    private:
        Level max_level = Level::DISABLE;
        std::string _name;
    };
};
#endif //__LOG_H__