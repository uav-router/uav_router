#ifndef _MEASURE_H_
#define _MEASURE_H_

#include <string>
#include <string_view>
#include <chrono>
#include <variant>
#include <sstream>
#include <memory>

// Class for create single measurement
class Metric {
public:
    Metric(std::string name);
    ~Metric() = default;

    auto tag(std::string_view key, std::string_view value) -> Metric&&;
    auto field(std::string_view name, std::variant<int, long long int, std::string, double, std::chrono::nanoseconds> value) -> Metric&&;
    void add_tag(std::string_view key, std::string_view value);
    void add_field(std::string_view name, std::variant<int, long long int, std::string, double, std::chrono::nanoseconds> value);
    auto time(std::chrono::time_point<std::chrono::system_clock> stamp) -> Metric&&;

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
    virtual void send(Metric&& metric) = 0;
    virtual void flush() = 0;
    virtual ~OStat() = default;
};

// Base class to send collected stats
class Stat {
public:
    virtual void report(OStat& out) = 0;
    virtual ~Stat() = default;
};

class DurationCollector {
public:
    using Duration = std::chrono::nanoseconds;
    void add_metric(Duration interval) {
        if (interval>max) { max = interval;
        } else if (interval<min) { min = interval;
        }
        count++;
        all += interval;
    }
    void report(Metric& out, std::string name) {
        if (!count) return;
        out.add_field(name+"_t",all);
        out.add_field(name+"_max",max);
        out.add_field(name+"_min",min);
        out.add_field(name+"_cnt",count);
        min = Duration::max();
        max = Duration::zero();
    }

    class Measure {
    public:
        using Clock = std::chrono::steady_clock;
        Measure(DurationCollector& collector):_collector(collector),_start(Clock::now()) {}
        ~Measure() {
            _collector.add_metric(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-_start));
        }
        DurationCollector& _collector;
        Clock::time_point _start;
    };
    auto measure() -> std::unique_ptr<Measure> {
        return std::make_unique<Measure>(*this);
    }
    Duration all = Duration::zero();
    Duration min = Duration::max();
    Duration max = Duration::zero();
    int count = 0;
};

#endif // _MEASURE_H_
