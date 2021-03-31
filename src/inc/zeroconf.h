#ifndef __ZEROCONF_INC_H__
#define __ZEROCONF_INC_H__
#include "../avahi-cpp.h"

class Avahi : public AvahiHandler {
public:
    virtual auto query_service_name(SockAddr& addr, int type) -> std::string = 0;
};

#endif  //!__ZEROCONF_INC_H__