#ifndef __FILTERBASE__H__
#define __FILTERBASE__H__
#include "../inc/endpoints.h"
#include "../impl/statobj.h"
class FilterBase : public Filter {
public:
    FilterBase(std::string name):cnt(std::make_shared<StatCounters>(std::move(name))) {}
    auto stat() -> std::shared_ptr<Stat> override {
        return cnt;
    }
    auto write_next(const void* buf, int len) -> int override {
        cnt->add("next", len);
        cnt->add("pack", 1);
        return Filter::write_next(buf, len);
    }
    auto write_rest(const void* buf, int len) -> int override {
        cnt->add("rest", len);
        return Filter::write_rest(buf, len);
    }
protected:
    std::shared_ptr<StatCounters> cnt;
    int packet_len = 0;
};
#endif  //!__FILTERBASE__H__