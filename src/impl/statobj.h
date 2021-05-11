#ifndef __STATOBJ__H__
#define __STATOBJ__H__
#include <initializer_list>
#include <map>
#include <deque>
#include <string>
#include <forward_list>
#include "../inc/stat.h"

class DurationCollector {
public:
    using Duration = std::chrono::nanoseconds;
    void add_metric(Duration interval) {
        count++;
        all += interval;
    }
    void report(Metric& out, std::string name) {
        if (!count) return;
        if (all!=Duration::zero()) out.add_field(name+"_t",all);
        if (count!=0) out.add_field(name+"_cnt",count);
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
    int count = 0;
};

class StatDurations : public Stat {
public:
    StatDurations(std::string name):_name(std::move(name)) {}
    void report(OStat& out) override {
        Metric meter(_name);
        for(auto& tag: tags) {
            meter.add_tag(tag.first, tag.second);
        }
        report(meter);
        out.send(std::move(meter));
    }
    void report(Metric& meter) {
        for(auto& item : time) {
            item.second.report(meter,item.first);
        }
    }
    std::map<std::string,DurationCollector> time;
    std::forward_list<std::pair<std::string,std::string>> tags;
private:
    std::string _name;
};

class StatCounters : public Stat {
public:
    StatCounters(std::string name):_name(std::move(name)) {}
    void report(OStat& out) override {
        Metric meter(_name);
        for(auto& tag: tags) {
            meter.add_tag(tag.first, tag.second);
        }
        report(meter);
        out.send(std::move(meter));
    }
    void report(Metric& meter) {
        for(auto& item : values) {
            if (item.second.second) {
                meter.add_field(item.first, item.second.first);
                item.second.second = false;
            }
        }
    }
    void add(const std::string& name, int value) {
        auto& v = values[name];
        v.first += value;
        v.second = true;
    }

    std::forward_list<std::pair<std::string,std::string>> tags;
private:
    std::map<std::string,std::pair<int,bool>> values;
    std::string _name;
};

class StatEvents : public Stat {
public:
    StatEvents(std::string name, std::initializer_list<std::map<int,std::string>::value_type> names):_name(std::move(name)),_names(names) {}
    auto add(int index) -> Metric& {
        std::string name = _names[index];
        if (name.empty()) name = "ev_"+std::to_string(index);
        _events.emplace_front(_name);
        _events.front().add_field(name, ++_counter[index]);
        while (_events.size()>max_events) _events.pop_back();
        return _events.front();
    }
    void report(OStat& out) override {
        while(_events.size()) {
            out.send(std::move(_events.back()));
            _events.pop_back();
        }
    }
private:
    std::string _name;
    std::map<int,std::string> _names;
    std::map<int,int> _counter;
    std::deque<Metric> _events;
    int max_events = 20;
};

#endif  //!__STATOBJ__H__