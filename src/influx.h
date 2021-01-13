#ifndef __INFLUX_H__
#define __INFLUX_H__
#include "measure.h"
#include "loop.h"
class InfluxOStat : public OStat {
public:
    virtual void init(  const std::string& host, 
                uint16_t port, 
                IOLoop* loop,
                std::string global_tags = "",
                int pack_size = 400,
                int queue_max_size = 10000,
                int queue_shrink_size = 9900
                ) = 0;
    static auto create() -> std::unique_ptr<InfluxOStat>;
};

#endif //__INFLUX_H__