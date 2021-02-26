//#include <cerrno>
#include <system_error>
#include <netdb.h>
#include <string>
#include <iostream>
#include <avahi-common/error.h>
#include "err.h"
#include "log.h"
#include <regex>



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

class regex_category_impl: public std::error_category
{
public:
    [[nodiscard]]  auto name() const noexcept -> const char * override {
        return "regex";
    }
    [[nodiscard]]  auto message(int ev) const -> std::string override {
        switch(ev) {
          case std::regex_constants::error_type::_S_error_collate: return "The expression contained an invalid collating element name.";
          case std::regex_constants::error_type::_S_error_ctype: return "The expression contained an invalid character class name.";
          case std::regex_constants::error_type::_S_error_escape: return "The expression contained an invalid escaped character, or a trailing escape.";
          case std::regex_constants::error_type::_S_error_backref: return "The expression contained an invalid back reference.";
          case std::regex_constants::error_type::_S_error_brack: return "The expression contained mismatched [ and ].";
          case std::regex_constants::error_type::_S_error_paren: return "The expression contained mismatched ( and ).";
          case std::regex_constants::error_type::_S_error_brace: return "The expression contained mismatched { and }";
          case std::regex_constants::error_type::_S_error_badbrace: return "The expression contained an invalid range in a {} expression.";
          case std::regex_constants::error_type::_S_error_range: return "The expression contained an invalid character range, such as [b-a] in most encodings.";
          case std::regex_constants::error_type::_S_error_space: return "There was insufficient memory to convert the expression into a finite state machine.";
          case std::regex_constants::error_type::_S_error_badrepeat: return "One of <em>*?+{</em> was not preceded by a valid regular expression.";
          case std::regex_constants::error_type::_S_error_complexity: return "The complexity of an attempted match against a regular expression exceeded a pre-set level.";
          case std::regex_constants::error_type::_S_error_stack: return "There was insufficient memory to determine whether the regular expression could match the specified character sequence.";
        }
        return "Unknown regex error";
    }
};

regex_category_impl regex_category_instance;

auto regex_category() -> const std::error_category & {
    return avahi_category_instance;
}

regex_code::regex_code(int val, const std::string& place):error_c(val, regex_category(), place) {}


auto error_handler::on_error(error_c& ec, const std::string& place) -> bool {
    if (!ec) return false;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        Log::error()<<ec<<std::endl;
    }
    return true;
}

auto error_handler::on_error(error_c ec) -> bool {
    if (!ec) return false;
    if (_on_error) { _on_error(ec);
    } else {
        Log::error()<<ec<<std::endl;
    }
    return true;
}


auto error_handler::on_error(int ret, const std::string& place) -> bool {
    if (!ret) return false;
    errno_c ec;
    if (!place.empty()) ec.add_place(place);
    if (_on_error) { _on_error(ec);
    } else {
        Log::error()<<ec<<std::endl;
    }
    return true;
}
auto error_c::place() const -> std::string { return _place; }

auto operator<<(std::ostream& out, const error_c& ec ) -> std::ostream& {
    out<<ec.place()<<": "<<ec.message();
    return out;
}

