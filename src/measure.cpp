#include "measure.h"

#include <iostream>
#include <chrono>
#include <memory>
#include <sstream>

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

Measure::Measure(std::string name):_name(std::move(name)),_time(std::chrono::system_clock::now()) {}

auto Measure::field(std::string_view name, std::variant<int, long long int, std::string, double> value) -> Measure&& {
    if (_fields.tellp()!=0) _fields << ",";
    _fields << name << "=";
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>)
            _fields << v << 'i';
        else if constexpr (std::is_same_v<T, long long int>)
            _fields << v << 'i';
        else if constexpr (std::is_same_v<T, double>)
            _fields << v;
        else if constexpr (std::is_same_v<T, std::string>)
            _fields << '"' << v << '"';
      }, value);
    return std::move(*this);
}

auto Measure::tag(std::string_view key, std::string_view value) -> Measure&& {
    _tags <<"," << key << "=" << value;
    return std::move(*this);
}

auto Measure::time(std::chrono::time_point<std::chrono::system_clock> stamp) -> Measure&& {
    _time = stamp;
    return std::move(*this);
}

void Measure::to_stream(std::ostream& out, std::string_view global_tags) {
    using namespace std::chrono;
    out<<_name<<_tags.str();
    if (global_tags.size()!=0) { out<<global_tags;
    }
    out<<' '<<_fields.str()<<' ';
    out<<std::to_string(duration_cast<nanoseconds>(_time.time_since_epoch()).count());
}
