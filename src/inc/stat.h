#ifndef __STAT_INC_H__
#define __STAT_INC_H__
#include "metric.h"
#include <memory>

// Class to send measurements
class OStat {
public:
    virtual void send(Metric&& metric) = 0;
    virtual void flush() = 0;
    virtual ~OStat() = default;
};


// Base class to send collected stats
class Stat {
public:
    virtual void report(OStat& out) = 0;
    virtual ~Stat() = default;
};

class OStatEndpoint : public OStat {
public:
    virtual void pack_size(int size) = 0; // default: 400
    virtual void queue_size(int max, int shrink) = 0; // defaults: max = 10000 shrink = 9900
    virtual void global_tags(std::string values) = 0;
};

class StatHandler {
public:
    virtual auto influx_udp(const std::string& host, uint16_t port) -> std::unique_ptr<OStatEndpoint> = 0;
    virtual auto influx_line(const std::string& filename) -> std::unique_ptr<OStatEndpoint> = 0;
    virtual void add_output(std::unique_ptr<OStat> out) = 0;
    virtual void clear_outputs() = 0;
};

#endif  //!__STAT_INC_H__