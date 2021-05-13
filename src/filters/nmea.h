#ifndef __NMEA__H__
#define __NMEA__H__
#include "../inc/endpoints.h"
#include <cstdint>
#include <cstring>
#include <limits>

class NMEA : public Filter {
    enum {PREAMBLE='$'};

    auto write(const void* buf, int len) -> int override {
        auto* ptr = (uint8_t*)buf;
        auto ret = len;
        while(len) {
            switch (state) {
                case BEFORE: {
                    auto* preamble = (uint8_t*)memchr(ptr,PREAMBLE,len);
                    if (preamble==nullptr) {
                        write_rest(ptr,len);
                        len = 0;
                        break;
                    }
                    if (preamble!=ptr) {
                        int l = preamble-ptr;
                        write_rest(ptr,l);
                        ptr+=l;
                        len-=l;
                    }
                    state = CR;
                    packet[0] = PREAMBLE;
                    packet_len = 1;
                    ptr++;
                    len--;
                } break;
                case CR: {
                    auto* cr = (uint8_t*)memchr(ptr,0x0d,len);
                    if (cr==nullptr) {
                        if (packet_len+len > sizeof(packet)-2) {
                            write_rest(packet.data(),1);
                            state = BEFORE;
                            write(packet.data()+1,packet_len-1);
                            break;
                        }
                        memmove(packet.data()+packet_len, ptr, len);
                        packet_len+=len;
                        ptr+=len;
                        len-=len;
                        break;
                    }
                    if (cr!=ptr) {
                        int l = cr-ptr;
                        if (packet_len+l > packet.size()-2) {
                            write_rest(packet.data(),1);
                            state = BEFORE;
                            write(packet.data()+1,packet_len-1);
                            break;
                        }
                        memmove(packet.data()+packet_len, ptr, l);
                        packet_len+=l;
                        ptr+=l;
                        len-=l;
                    }
                    state = LF;
                    packet[packet_len++] = 0x0d;
                    ptr++;
                    len--;
                } break;
                case LF: {
                    state = BEFORE;
                    if (*ptr==0x0a) {
                        packet[packet_len++] = 0x0a;
                        if (valid_checksum()) { 
                            write_next(packet.data(),packet_len);
                        } else {
                            write_rest(packet.data(),1);
                            write(packet.data()+1,packet_len-1);
                        }
                    } else {
                        write_rest(packet.data(),packet_len);
                    }
                } break;
            }
        }
        return ret;
    }
    auto valid_checksum() -> bool {
        if (packet[packet_len - 5] != '*') return false;
        int crc = 0;
        for (int i = 1; i < packet_len - 5; i ++) {
            crc ^= packet[i];
        }
        std::string crc_str{ char(packet[packet_len-4]), char(packet[packet_len-3])};
        return crc == strtol(crc_str.c_str(),nullptr,16);
    }
    auto stat() -> std::shared_ptr<Stat> override {
        return std::shared_ptr<Stat>();
    }
private:
    std::array<uint8_t,1024> packet;
    enum State {BEFORE,CR,LF};
    State state = BEFORE;
    int packet_len = 0;
};


#endif  //!__NMEA__H__
