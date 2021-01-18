#ifndef __ERROR_H__
#define __ERROR_H__

#include <system_error>
#include <netdb.h>
#include <functional>
#include <utility>

class error_c : public std::error_code {
public:
    error_c(int ec=0, const std::error_category& ecat=std::system_category(), std::string  place = "") noexcept 
        : std::error_code(ec, ecat), _place(std::move(place)) {}
    void add_place(const std::string& place) {
        _place = place + "->" + _place;
    }
    [[nodiscard]] auto place() const -> std::string;

  private:
    std::string _place;
};

class errno_c : public error_c {
public:
    errno_c(int val, const std::string& place = ""):error_c(val, std::system_category(),place) {}
    errno_c(const std::string& place = ""):error_c(errno, std::system_category(),place) {}
};

inline auto err_chk(int ret, const std::string& place="") -> errno_c {
    if (ret) return errno_c(place);
    return errno_c(0);
}


auto eai_category() -> const std::error_category &;

class eai_code : public error_c {
public:
    eai_code(int val=0, const std::string& place="");
    eai_code(gaicb* req, const std::string& place="");
};

inline void eai_check(const eai_code& ret, const std::string& from) {
    if (ret) throw std::system_error(ret, from);
}

class error_handler {
public:
    using callback_t = std::function<void(error_c&)>;
    virtual ~error_handler() = default;
    inline void on_error(callback_t func) { _on_error = func;
    }
protected:
    auto on_error(error_c& ec, const std::string& place = "") -> bool;
    auto on_error(int ret, const std::string& place = "") -> bool;
private:
    callback_t _on_error;
};

#endif //__ERROR_H__