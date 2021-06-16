#ifndef __HEX__H__
#define __HEX__H__
#include "../inc/endpoints.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include "filterbase.h"

class Hex : public FilterBase {
public:
    Hex():FilterBase("hex") {}
#ifdef  YAML_CONFIG
    auto init_yaml(YAML::Node cfg) -> error_c override {
        return error_c();
    }
#endif  //YAML_CONFIG

    auto write(const void* buf, int len) -> int override {
        int bytes_out = 0;
        auto* b = (char*)buf;
        int l = len;
        std::stringstream s_out;
        std::stringstream s_raw;
        std::stringstream s_hex;
        
        while(l) {
            int pack = std::min(l,16);
            s_out.setf(std::ios::right,std::ios::adjustfield);
            s_out<<std::hex<<std::setw(4)<<std::setfill('0')<<bytes_out<<" : ";
            s_out.setf(std::ios::left,std::ios::adjustfield);
            while(pack) {
                s_raw<<(std::isprint(*b) ? *b : '.');
                s_hex<<std::hex<<std::setw(2)<<std::setfill('0')<<int(*(uint8_t*)b)<<' ';
                pack--;
                l--;
                bytes_out++;
                b++;
            }
            s_out<<std::setw(17)<<std::setfill(' ')<<s_raw.str()<<std::setw(16*3)<<s_hex.str()<<std::endl;
            s_raw.str("");
            s_hex.str("");
        }
        std::string packet = s_out.str();
        write_next(packet.data(),packet.size());
        return len;
    }
};


#endif  //!__HEX__H__
