#include "filters/mavlink1.h"
#include "filters/nmea.h"
#include "filters/rtcm3.h"
#include "filters/ubx.h"
#include <memory>

namespace Filters {
    auto create(std::string name) -> std::shared_ptr<Filter> {
        if (name=="mavlink1") return std::make_shared<Mavlink_v1>();
        if (name=="nmea") return std::make_shared<NMEA>();
        if (name=="rtcm3") return std::make_shared<RTCM_v3>();
        if (name=="ubx") return std::make_shared<UBX>();
        return std::shared_ptr<Filter>();
    }

    void register_plugin(std::string name) {

    }
};