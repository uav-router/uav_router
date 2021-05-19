#ifndef __UBX__H__
#define __UBX__H__
#include "../inc/endpoints.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <array>
#include "filterbase.h"

class UBX : public FilterBase {
public:
    enum {PREAMBLE0=0xb5, PREAMBLE1=0x62};
    UBX():FilterBase("UBX") {}
    auto write(const void* buf, int len) -> int override {
        auto* ptr = (uint8_t*)buf;
        auto ret = len;
        while(len) {
            switch (state) {
                case BEFORE: {
                    auto* preamble0 = (uint8_t*)memchr(ptr,PREAMBLE0,len);
                    if (preamble0==nullptr) {
                        write_rest(ptr,len);
                        len = 0;
                        break;
                    }
                    if (preamble0!=ptr) {
                        int l = preamble0-ptr;
                        write_rest(ptr,l);
                        ptr+=l;
                        len-=l;
                    }
                    state = PREAMBLE;
                    packet[0] = PREAMBLE0;
                    ptr++;
                    len--;
                } break;
                case PREAMBLE: {
                    if (ptr[0]==PREAMBLE1) {
                        packet[1]=PREAMBLE1;
                        ptr++;
                        len--;
                        state = HEADER;
                        packet_len = 2;
                        break;
                    }
                    state = BEFORE;
                    write_rest(packet.data(),1);
                } break;
                case HEADER: {
                    int copy_len = std::min(6 - packet_len,len);
                    memmove(packet.data()+packet_len, ptr, copy_len);
                    packet_len+=copy_len;
                    ptr+=copy_len;
                    len-=copy_len;
                    if (packet_len==6) {
                        state = LOAD;
                        payload_len = packet[4] + (packet[5]>>8);
                    }
                } break;
                case LOAD: {
                    int size = 8 + payload_len;
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
                            cnt->add("badcrc",1);
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
        uint8_t* ptr = packet.data() + 2;
        int len = payload_len + 4;
        uint8_t crc1 = 0;
        uint8_t crc2 = 0;
        while(len--) { crc2 += (crc1 += *ptr++);
        }
        uint8_t* crc = packet.data()+6+payload_len;
        return (crc1==*crc) && (crc2==*(crc+1));
    }
    auto stat() -> std::shared_ptr<Stat> override {
        return std::shared_ptr<Stat>();
    }
private:
    std::array<uint8_t,std::numeric_limits<uint16_t>::max()+8> packet;
    enum State {BEFORE,PREAMBLE, HEADER, LOAD};
    State state = BEFORE;
    int payload_len = 0;
    int packet_len = 0;
};

#endif  //!__UBX__H__
