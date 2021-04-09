#ifndef __ADDRESS_INC_H__
#define __ADDRESS_INC_H__

#include "../sockaddr.h"

class AddressResolver : public error_handler {
public:
    using OnResolveFunc  = std::function<void(SockAddrList&&)>;
    virtual auto init(const std::string& host, uint16_t port, OnResolveFunc handler) -> error_c = 0;
    virtual auto family(int f) -> AddressResolver& = 0;
    virtual auto socktype(int t) -> AddressResolver& = 0;
    virtual auto protocol(int p) -> AddressResolver& = 0;
    virtual auto flags(int f) -> AddressResolver& = 0;
    virtual auto add_flags(int f) -> AddressResolver& = 0;
    virtual auto requery() -> error_c = 0;
};
#endif  //!__ADDRESS_INC_H__