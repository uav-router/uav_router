#ifndef __ERROR_H__
#define __ERROR_H__

#include <system_error>
#include <netdb.h>
#include <functional>
#include <utility>
#include <iostream>

class error_c : public std::error_code {
public:
    error_c(int ec=0, const std::error_category& ecat=std::system_category(), std::string  context = "") noexcept 
        : std::error_code(ec, ecat), _context(std::move(context)) {}
    void add_context(const std::string& context) {
        _context = context + "->" + _context;
    }
    [[nodiscard]] auto context() const -> std::string;

private:
    std::string _context;
};

auto operator<<(std::ostream& out, const error_c& ec ) -> std::ostream&;

class errno_c : public error_c {
public:
    errno_c(int val, const std::string& context = ""):error_c(val, std::system_category(),context) {}
    errno_c(const std::string& context = ""):error_c(errno, std::system_category(),context) {}
};

inline auto to_errno_c(int ret, const std::string& context="") -> errno_c {
    if (ret) return errno_c(context);
    return errno_c(0);
}

class eai_code : public error_c {
public:
    eai_code(int val=0, const std::string& context="");
    eai_code(gaicb* req, const std::string& context="");
};

class avahi_code : public error_c {
public:
    avahi_code(int val=0, const std::string& context="");
};

class regex_code : public error_c {
public:
    regex_code(int val=0, const std::string& context="");
};

class error_handler {
public:
    using callback_t = std::function<void(error_c&)>;
    virtual ~error_handler() = default;
    inline void on_error(callback_t func) { _on_error = func;
    }
protected:
    auto on_error(error_c& ec, const std::string& context) -> bool;
    auto on_error(error_c ec) -> bool;
    auto on_error(int ret, const std::string& context = "") -> bool;
private:
    callback_t _on_error;
};

#endif //__ERROR_H__