//#include <cerrno>
#include <system_error>
#include <netdb.h>
#include <string>
#include <iostream>
#include <avahi-common/error.h>
#include "err.h"
#include "log.h"



class eai_category_impl: public std::error_category
{
public:
    [[nodiscard]]  auto name() const noexcept -> const char * override {
        return "addrinfo";
    }
    [[nodiscard]]  auto message(int ev) const -> std::string override {
        return gai_strerror(ev);
    }
};

eai_category_impl eai_category_instance;

auto eai_category() -> const std::error_category & {
    return eai_category_instance;
}

eai_code::eai_code(int val, const std::string& place):error_c(val, eai_category(), place) {}
eai_code::eai_code(gaicb* req, const std::string& place):error_c(gai_error(req), eai_category(), place) {}

class avahi_category_impl: public std::error_category
{
public:
    [[nodiscard]]  auto name() const noexcept -> const char * override {
        return "avahi";
    }
    [[nodiscard]]  auto message(int ev) const -> std::string override {
        return avahi_strerror(ev);
    }
};

avahi_category_impl avahi_category_instance;

auto avahi_category() -> const std::error_category & {
    return avahi_category_instance;
}

avahi_code::avahi_code(int val, const std::string& place):error_c(val, avahi_category(), place) {}

auto error_handler::on_error(error_c& ec, const std::string& place) -> bool {
    if (!ec) return false;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        log::error()<<ec<<std::endl;
    }
    return true;
}

auto error_handler::on_error(int ret, const std::string& place) -> bool {
    if (!ret) return false;
    errno_c ec;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        log::error()<<ec<<std::endl;
    }
    return true;
}
auto error_c::place() const -> std::string { return _place; }

auto operator<<(std::ostream& out, const error_c& ec ) -> std::ostream& {
    out<<ec.place()<<": "<<ec.message();
    return out;
}

