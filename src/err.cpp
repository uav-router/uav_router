//#include <cerrno>
#include <system_error>
#include <netdb.h>
#include <string>
//#include <iostream>
#include "err.h"
#include "log.h"



class eai_category_impl:
    public std::error_category
{
public:
    virtual const char * name() const noexcept {
        return "addrinfo";
    }
    virtual std::string message(int ev) const {
        return gai_strerror(ev);
    }
};

eai_category_impl eai_category_instance;

const std::error_category & eai_category() {
    return eai_category_instance;
}

eai_code::eai_code(int val, const std::string& place):error_c(val, eai_category(), place) {}
eai_code::eai_code(gaicb* req, const std::string& place):error_c(gai_error(req), eai_category(), place) {}

bool error_handler::on_error(error_c& ec, const std::string& place) {
    if (!ec) return false;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        log::error()<<ec.place()<<": "<<ec.message()<<std::endl;
    }
    return true;
}

bool error_handler::on_error(int ret, const std::string& place) {
    if (!ret) return false;
    errno_c ec;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        log::error()<<ec.place()<<": "<<ec.message()<<std::endl;
    }
    return true;
}