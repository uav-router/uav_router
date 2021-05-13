#ifndef __RTCM3__H__
#define __RTCM3__H__
#include "../inc/endpoints.h"
#include <cstdint>
#include <cstring>
#include <limits>

auto crc24(const uint8_t *bytes, uint16_t len) -> uint32_t {
    uint32_t crc = 0;
    while (len--) {
        const uint8_t idx = (crc>>16) ^ *bytes++;
        uint32_t crct = idx<<16;
        for (uint8_t j=0; j<8; j++) {
            crct <<= 1;
            if (crct & 0x1000000) {
                crct ^= 0x1864CFB;
            }
        }
        crc = ((crc<<8)&0xFFFFFF) ^ crct;
    }
    return crc;
}


class RTCM_v3 : public Filter {
    enum {PREAMBLE=0xD3};

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
                    state = HEADER;
                    packet[0] = PREAMBLE;
                    ptr++;
                    len--;
                    packet_len = 1;
                } break;
                case HEADER: {
                    int copy_len = std::min(3 - packet_len,len);
                    memmove(packet.data()+packet_len, ptr, copy_len);
                    packet_len+=copy_len;
                    ptr+=copy_len;
                    len-=copy_len;
                    if (packet_len==3) {
                        state = LOAD;
                        payload_len = (packet[1]<<8 | packet[2]) & 0x3ff;
                    }
                } break;
                case LOAD: {
                    int size = 6 + payload_len;
                    int copy_len = std::min(size - packet_len,len);
                    memmove(packet.data()+packet_len, ptr, copy_len);
                    packet_len+=copy_len;
                    ptr+=copy_len;
                    len-=copy_len;
                    if (packet_len==size) {
                        state = BEFORE;
                        if (valid_checksum()) { 
                            write_next(packet.data(),packet_len);
                        } else {
                            write_rest(packet.data(),2);
                            write(packet.data()+2,packet_len-2);
                        }
                    }
                } break;
            }
        }
        return ret;
    }
    auto valid_checksum() -> bool {
        uint8_t* crc = packet.data()+3+payload_len;
        uint32_t crc1 = (crc[0] << 16) | (crc[1] << 8) | crc[2];
        uint32_t crc2 = crc24(packet.data(), payload_len+3);
        return crc1==crc2;
    }
    auto stat() -> std::shared_ptr<Stat> override {
        return std::shared_ptr<Stat>();
    }
private:
    std::array<uint8_t,0x3ff+6> packet;
    enum State {BEFORE, HEADER, LOAD};
    State state = BEFORE;
    int payload_len = 0;
    int packet_len = 0;
};

#endif  //!__RTCM3__H__
