#ifndef __MAVLINK1__H__
#define __MAVLINK1__H__
#include "../inc/endpoints.h"
#include <cstdint>
#include <cstring>
#include <memory>

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
                        } else { cnt->add("badcrc",1);
                        }
                        write_rest(packet.data(),1);
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
        return (packet[packet_len-2]+(packet[packet_len-1]>>8))==crc;
    }
private:
    std::array<uint8_t,263> packet;
    uint8_t* _crc_extra = nullptr; 
    enum State {BEFORE,LEN, LOAD};
    State state = BEFORE;
    int packet_len = 0;
};


#endif  //!__MAVLINK1__H__
