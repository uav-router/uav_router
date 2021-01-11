#ifndef _MEASURE_H_
#define _MEASURE_H_

#include <string>
#include <string_view>
#include <chrono>
#include <variant>
#include <sstream>
#include <memory>

// Class for create single measurement
class Measure
{
  public:
    Measure(std::string name);
    ~Measure() = default;

    auto tag(std::string_view key, std::string_view value) -> Measure&&;
    auto field(std::string_view name, std::variant<int, long long int, std::string, double> value) -> Measure&&;
    auto time(std::chrono::time_point<std::chrono::system_clock> stamp) -> Measure&&;

    void to_stream(std::ostream& out, std::string_view global_tags="");

  private:
    std::string _name;
    std::chrono::time_point<std::chrono::system_clock> _time;
    std::stringstream _tags;
    std::stringstream _fields;
};

// Class to send measurements
class OStat {
public:
    virtual void send(Measure&& metric) = 0;
    virtual void flush() = 0;
    virtual ~OStat() = default;
};

// Base class to send collected stats
class Stat {
public:
    virtual void report(OStat& out) = 0;
    virtual ~Stat() = default;
};

#endif // _MEASURE_H_
