#include "measure.h"

#include <iostream>
#include <chrono>
#include <memory>
#include <sstream>

class Measure::MeasureImpl {
public:
    MeasureImpl(std::string name):_name(std::move(name)),_time(std::chrono::system_clock::now()) {}

    void to_stream(std::ostream& out, std::string_view global_tags) {
        using namespace std::chrono;
        bool local_tags_exists = _tags.tellp()!=0;
        out<<_name<<_tags.str();
        if (global_tags.size()!=0) {
            if (local_tags_exists) out<<',';
            out<<global_tags;
        }
        out<<' '<<_fields.str()<<' ';
        out<<std::to_string(duration_cast<nanoseconds>(_time.time_since_epoch()).count());
    }

    std::string _name;
    std::chrono::time_point<std::chrono::system_clock> _time;
    std::stringstream _tags;
    std::stringstream _fields;
};

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

Measure::Measure(std::string name):_impl{new MeasureImpl{std::move(name)}} {}

auto Measure::field(std::string_view name, std::variant<int, long long int, std::string, double> value) -> Measure&& {
    auto& fields = _impl->_fields;
    if (fields.tellp()!=0) fields << ",";
    fields << name << "=";
    std::visit([&fields](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int>)
            fields << v << 'i';
        else if constexpr (std::is_same_v<T, long long int>)
            fields << v << 'i';
        else if constexpr (std::is_same_v<T, double>)
            fields << v;
        else if constexpr (std::is_same_v<T, std::string>)
            fields << '"' << v << '"';
      }, value);
    return std::move(*this);
}

auto Measure::tag(std::string_view key, std::string_view value) -> Measure&& {
    _impl->_tags <<"," << key << "=" << value;
    return std::move(*this);
}

auto Measure::time(std::chrono::time_point<std::chrono::system_clock> stamp) -> Measure&& {
  _impl->_time = stamp;
  return std::move(*this);
}

void Measure::to_stream(std::ostream& out, std::string_view global_tags) {
    _impl->to_stream(out,global_tags);
}
