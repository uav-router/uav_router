#ifndef __MAVLINK1__H__
#define __MAVLINK1__H__
#include "../inc/endpoints.h"
#include "../log.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <map>
#include <chrono>

#include "filterbase.h"

enum {X25_INIT_CRC=0xffff};

static inline void crc_accumulate(uint8_t data, uint16_t *crc) {
        uint8_t tmp = data ^ (uint8_t)(*crc & 0xff);
        tmp ^= (tmp<<4);
        *crc = (*crc>>8) ^ (tmp<<8) ^ (tmp <<3) ^ (tmp>>4);
}

static inline auto crc_calculate(const uint8_t* buf, uint16_t len) -> uint16_t {
        uint16_t crc = X25_INIT_CRC;
        while (len--) {
            crc_accumulate(*buf++, &crc);
        }
        return crc;
}


std::array<uint8_t,256> crc_extra = 
                      { 50, 124, 137,   0, 237, 217, 104, 119,   0,   0, 
                         0,  89,   0,   0,   0,   0,   0,   0,   0,   0, 
                       214, 159, 220, 168,  24,  23, 170, 144,  67, 115, 
                        39, 246, 185, 104, 237, 244, 222, 212,   9, 254, 
                       230,  28,  28, 132, 221, 232,  11, 153,  41,  39, 
                         0,   0,   0,   0,  15,   3,   0,   0,   0,   0, 
                         0, 153, 183,  51,  82, 118, 148,  21,   0, 243, 
                       124,   0,   0,  38,  20, 158, 152, 143,   0,   0, 
                         0, 106,  49,  22,  29,  12, 241, 233,   0, 231, 
                       183,  63,   54,   0,   0,   0,   0,   0,   0,   0, 
                       175, 102,  158, 208,  56,  93, 211, 108,  32, 185, 
                        84,  34,    0, 124, 119,   4,  76, 128,  56, 116, 
                       134, 237,  203, 250,  87, 203, 220,  25, 226,   0, 
                        29, 223,   85,   6, 229, 203,   1,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,  13,  49,   0, 
                       134, 219,  208, 188,  84,  22,  19,  21, 134,   0, 
                        78,  68,  189, 127, 154,  21,  21, 144,   1, 234, 
                        73, 181,   22,  83, 167, 138, 234, 240,  47, 189, 
                        52, 174,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   0,   0, 
                         0,   0,    0,   0,   0,   0,   0,   0,   8, 204, 
                         49,  170, 44,  83,  46,   0};


class Mavlink_v1 : public FilterBase {
public:
    enum {STX=0xFE};
    Mavlink_v1(uint8_t* crc_array=crc_extra.data()):FilterBase("mavlink_v1"),_crc_extra(crc_array) {}
#ifdef  YAML_CONFIG
    auto init_yaml(YAML::Node cfg) -> error_c override {
        auto crcs = cfg["crc_extra"];
        _crc_extra = nullptr;
        if (crcs) {
            if (!crcs.IsSequence()) {
                Log::warning()<<"crc_extra is not array. Use default."<<Log::endl;
            } else {
                crc_holder = crcs.as<std::vector<uint8_t>>();
                if (crc_holder.size()<256) {
                    Log::warning()<<"crc_extra array size less than 256. Use zeroes for remaining messages."<<Log::endl;
                    crc_holder.resize(256,0);
                }
                _crc_extra = crc_holder.data();
            }
        }
        if (!_crc_extra) { _crc_extra = crc_extra.data();
        }
        return error_c();
    }
#endif  //YAML_CONFIG
    auto write(const void* buf, int len) -> int override {
        auto* ptr = (uint8_t*)buf;
        auto ret = len;
        while(len) {
            switch (state) {
                case BEFORE: {
                    auto* stx = (uint8_t*)memchr(ptr,STX,len);
                    if (stx==nullptr) {
                        write_rest(ptr,len);
                        len = 0;
                        break;
                    }
                    if (stx!=ptr) {
                        int l = stx-ptr;
                        write_rest(ptr,l);
                        ptr+=l;
                        len-=l;
                    }
                    state = LEN;
                    packet[0] = STX;
                    ptr++;
                    len--;
                } break;
                case LEN: {
                    packet[1]=ptr[0];
                    ptr++;
                    len--;
                    state = LOAD;
                    packet_len = 2;
                } break;
                case LOAD: {
                    int size = 8 + packet[1];
                    int copy_len = std::min(size - packet_len,len);
                    memmove(packet.data()+packet_len, ptr, copy_len);
                    packet_len+=copy_len;
                    ptr+=copy_len;
                    len-=copy_len;
                    if (packet_len==size) {
                        state = BEFORE;
                        if (valid_checksum()) {
                            write_next(packet.data(),packet_len);
                            break;
                        }
                        cnt->add("badcrc",1);
                        write_rest(packet.data(),1);
                        write(packet.data()+1,packet_len-1);
                    }
                } break;
            }
        }
        return ret;
    }
    auto valid_checksum() -> bool {
        auto crc = crc_calculate(packet.data()+1, packet[1]+5);
        if (_crc_extra) { crc_accumulate(_crc_extra[packet[5]], &crc);
        }
        return (packet[packet_len-2]+(packet[packet_len-1]<<8))==crc;
    }
private:
    std::array<uint8_t,263> packet;
    uint8_t* _crc_extra = nullptr;
    std::vector<uint8_t> crc_holder;
    enum State {BEFORE,LEN, LOAD};
    State state = BEFORE;
    int packet_len = 0;
};

class Mavlink_v1_filter : public FilterBase {
public:
    enum Type {SYSID, COMPID, SYSID_COMPID};
#ifdef  YAML_CONFIG
    void setup_filter(bool allow, YAML::Node cfg) {
        auto chapter = cfg["msgs"];
        if (chapter) {
            if (chapter.IsSequence()) {
                msg_filter(allow,chapter.as<std::vector<int>>());
            } else if (chapter.IsScalar()) {
                msg_filter(allow,{ chapter.as<int>() });
            } else {
                Log::error()<<"Mavlink msg filter values must be a sequence or scalar"<<Log::endl;
            }
        }
        chapter = cfg["sysid"];
        if (chapter) {
            if (chapter.IsSequence()) {
                sys_filter(allow, SYSID, chapter.as<std::vector<int>>());
            } else if (chapter.IsScalar()) {
                sys_filter(allow, SYSID, { chapter.as<int>() });
            } else {
                Log::error()<<"Mavlink msg filter values must be a sequence or scalar"<<Log::endl;
            }
        }
        chapter = cfg["compid"];
        if (chapter) {
            if (chapter.IsSequence()) {
                sys_filter(allow, COMPID, chapter.as<std::vector<int>>());
            } else if (chapter.IsScalar()) {
                sys_filter(allow, COMPID, { chapter.as<int>() });
            } else {
                Log::error()<<"Mavlink msg filter values must be a sequence or scalar"<<Log::endl;
            }
        }
        chapter = cfg["sys_compid"];
        if (chapter) {
            if (chapter.IsSequence()) {
                sys_filter(allow, SYSID_COMPID, chapter.as<std::vector<int>>());
            } else if (chapter.IsScalar()) {
                sys_filter(allow, SYSID_COMPID, { chapter.as<int>() });
            } else if (chapter.IsMap()) {
                std::vector<int> value;
                for(auto el : chapter) {
                    int sysid = el.first.as<int>();
                    if (el.second) {
                        if (el.second.IsScalar()) {
                            value.push_back(sysid+(el.second.as<int>()<<8));
                        } else if (el.second.IsSequence()) {
                            for (auto compid : el.second) {
                                value.push_back(sysid+(compid.as<int>()<<8));
                            }
                        }
                    }
                }
                if (value.size()) { sys_filter(allow, SYSID_COMPID, value);
                }
            } else {
                Log::error()<<"Mavlink msg filter values must be sequence, map or scalar"<<Log::endl;
            }
        }
    }
    auto init_yaml(YAML::Node cfg) -> error_c override {
        setup_filter(true, cfg["allow"]);
        setup_filter(false, cfg["deny"]);
        
        auto chapter = cfg["freq"];
        if (chapter) {
            if (!chapter.IsMap()){
                Log::error()<<"Mavlink filter 'freq' field must be a map"<<Log::endl;
            } else {
                freq_filter(chapter.as<std::map<int,int>>());
            }
        }

        return error_c();
    }
#endif  //YAML_CONFIG
    auto write(const void* buf, int len) -> int override {
        const auto* packet = (const uint8_t*)buf;
        if (passed(packet[3],packet[4],packet[5])) {
            return write_next(buf,len);
        }
        return write_rest(buf,len);
    }
    auto passed(uint8_t sysid, uint8_t compid, uint8_t msgid) -> bool {
        if (sys.size()) {
            int value = sysid;
            if (type==COMPID) { value = compid;
            } else if (type==SYSID_COMPID) { value += int(compid)<<8;
            }
            auto found = sys.find(value)!=sys.end();
            if (allow_sys ^ found) { 
                cnt->add("sysfilter",1);
                return false;
            }
        }
        if (msg.size()) {
            auto found = msg.find(msgid)!=sys.end();
            if (allow_msg ^ found) { 
                cnt->add("msgfilter",1);
                return false;
            }
        }
        if (silence_period.size()) {
            auto ptr = silence_period.find(msgid);
            if (ptr==silence_period.end()) { return true;
            }
            auto now = std::chrono::steady_clock::now();
            if ((now-last_send_time[msgid])<ptr->second) {
                cnt->add("freqfilter",1);
                return false;
            }
            last_send_time[msgid] = now;
        }
        return true;
    }
    void sys_filter(bool allow, Type t, std::vector<int> value) {
        allow_sys = allow;
        type = t;
        sys.clear();
        for(auto v: value) { sys.insert(v);
        }
    }

    void msg_filter(bool allow, std::vector<int> value) {
        allow_sys = allow;
        msg.clear();
        for(auto v: value) { msg.insert(v);
        }
    }

    void freq_filter(std::map<int,int> max_freq) {
        for(auto& el : max_freq) {
            silence_period[el.first] = std::chrono::nanoseconds(1000000000/el.second);
        }
    }
private:
    std::set<int> sys;
    bool allow_sys = false;
    Type type;
    std::set<int> msg;
    bool allow_msg = false;
    std::map<int,std::chrono::nanoseconds> silence_period;
    std::map<int,std::chrono::time_point<std::chrono::steady_clock>> last_send_time;
};


#endif  //!__MAVLINK1__H__
