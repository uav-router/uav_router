#ifndef __SIGNAL_INC_H__
#define __SIGNAL_INC_H__

#include <sys/signalfd.h>
#include <functional>
#include "../err.h"

class Signal : public error_handler {
public:
    using OnSignalFunc  = std::function<bool(signalfd_siginfo*)>;
    virtual auto init(std::initializer_list<int> signals, OnSignalFunc handler) -> error_c = 0;
};

#endif  //!__SIGNAL_INC_H__