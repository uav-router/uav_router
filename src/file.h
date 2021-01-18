#ifndef __FILE_H__
#define __FILE_H__
#include <memory>

#include "err.h"
#include "loop.h"

class OFile : public IOWriteable, public error_handler {
public:
    virtual void init(const std::string& filename) = 0;
    static auto create() -> std::unique_ptr<OFile>;
};
#endif //__FILE_H__