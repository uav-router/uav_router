#ifndef __INFLUX_H__
#define __INFLUX_H__
#include <string>
#include <memory>
#include "measure.h"
#include "epoll.h"

class InfluxOStat : public OStat {
public:
    virtual void init(const std::string& host, uint16_t port, IOLoop* loop);
    static auto create() -> std::unique_ptr<InfluxOStat>;
};

#endif //__INFLUX_H__