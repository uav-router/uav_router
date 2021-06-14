#include "filters/mavlink1.h"
#include "filters/nmea.h"
#include "filters/rtcm3.h"
#include "filters/ubx.h"
#include "filters/hex.h"
#include "log.h"
#include <memory>

Log::Log flog {"filters"};
namespace Filters {
    auto create(std::string name) -> std::shared_ptr<Filter> {
        if (name=="mavlink1") return std::make_shared<Mavlink_v1>();
        if (name=="nmea") return std::make_shared<NMEA>();
        if (name=="rtcm3") return std::make_shared<RTCM_v3>();
        if (name=="ubx") return std::make_shared<UBX>();
        if (name=="hex") return std::make_shared<Hex>();
        return std::shared_ptr<Filter>();
    }
#ifdef  YAML_CONFIG
    auto create(std::string name, YAML::Node cfg) -> std::shared_ptr<Filter> {
        auto flt = create(name);
        if (flt) {
            error_c ret = flt->init_yaml(cfg);
            if (!ret) return flt;
            flog.error()<<"Filter "<<name<<" creation error:"<<ret<<std::endl;
        }
        return std::shared_ptr<Filter>();
    }
#endif  //YAML_CONFIG

    void register_plugin(std::string name) {

    }
};