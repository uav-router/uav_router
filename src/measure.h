#ifndef INFLUX_MEASURE_H
#define INFLUX_MEASURE_H

#include <string>
#include <string_view>
#include <chrono>
#include <variant>
#include <sstream>
#include <memory>

namespace influxdb
{

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
    class MeasureImpl;
    std::unique_ptr<MeasureImpl> _impl;
};

} // namespace influxdb

#endif // INFLUX_MEASURE_H
