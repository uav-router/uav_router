#ifndef __STAT_IMPL_H__
#define __STAT_IMPL_H__
#include <forward_list>
#include <map>
#include <memory>
#include "../loop.h"
#include "influx.h"

//class OStatSet : public OStat {
//public:
//    void send(Metric&& metric) override {
//        for(auto& ostat:ostats) ostat->send(std::forward<Metric>(metric));
//    }
//    void flush() override {
//        for(auto& ostat:ostats) ostat->flush();
//    }
//    std::forward_list<std::unique_ptr<OStat>> ostats;
//};

class PeriodicStatCall {
public:
    void init(std::unique_ptr<Timer> timer, std::chrono::nanoseconds period, std::shared_ptr<InfluxStream>* sink) {
        _timer = std::move(timer);
        _period = period;
        _sink = sink;
        _timer->shoot([this](){
            auto it = _stats.before_begin();
            if (!_sink) return;
            if (!*_sink) return;
            for(auto p = _stats.begin();p!=_stats.end();p=std::next(it)) {
                if (p->expired()) {
                    p = _stats.erase_after(it);
                    continue;
                }
                p->lock()->report(*_sink->get());
                it = p;
            }
            if (_stats.empty()) { _timer->stop();
            }
        });
    }
    void start() {
        if (!_timer->armed()) _timer->arm_periodic(_period);
    }
    void stop() { _timer->stop();
    }
    void add(std::shared_ptr<Stat> source) {
        _stats.push_front(source);
        start();
    }
private:
    std::unique_ptr<Timer> _timer; 
    std::chrono::nanoseconds _period; 
    std::shared_ptr<InfluxStream> *_sink = nullptr;
    std::forward_list<std::weak_ptr<Stat>> _stats;
};

class StatHandlerImpl : public StatHandler, public error_handler {
public:
    StatHandlerImpl(IOLoop* loop):_loop(loop) {}

    auto stat() -> std::shared_ptr<OStatEndpoint> override {
        if (!output) {
            output = std::make_shared<InfluxStream>();
        }
        return output;
    }
    void set_output(std::shared_ptr<Writeable> out) override {
        if (!output) {
            output = std::make_shared<InfluxStream>();
        }
        output->init(out);
    }
#ifdef YAML_CONFIG
    auto init_yaml(std::shared_ptr<Writeable> out, YAML::Node cfg) -> error_c override {
        set_output(out);
        if (cfg && cfg.IsMap()) {
            auto packsize = cfg["packsize"];
            if (packsize && packsize.IsScalar()) {
                output->pack_size(packsize.as<int>());
            }
            auto queue = cfg["queue"];
            if (queue && queue.IsMap()) {
                int max = 10000;
                int shrink = 9900;
                auto entry = queue["max"];
                if (entry && entry.IsScalar()) {
                    max = entry.as<int>();
                }
                entry = queue["shrink"];
                if (entry && entry.IsScalar()) {
                    shrink = entry.as<int>();
                }
                if (max && shrink) {
                    output->queue_size(max, shrink);
                }
                entry = queue["tags"];
                if (entry && entry.IsMap()) {
                    std::stringstream tags;
                    for (auto tag : entry) {
                        if (tag.second.IsScalar()) {
                            tags<<","<<tag.first.as<std::string>()<<"="<<tag.second.as<std::string>();
                        }
                    }
                    auto t = tags.str();
                    if (t.size()) { output->global_tags(t);
                    }
                }
            }
        }
        return error_c();
    }
#endif //YAML_CONFIG

    void clear_outputs() override {
        output.reset();
    }

    void register_report(std::shared_ptr<Stat> source, std::chrono::nanoseconds period) override {
        auto it = statcalls.find(period);
        bool not_found = it==statcalls.end();
        auto& statcall = statcalls[period];
        if (not_found) {
            auto timer = _loop->timer();
            timer->on_error([this](const error_c& ec) {on_error(ec);});
            statcall.init(std::move(timer),period,&output);
        }
        statcall.add(source);
    }
private:
    IOLoop* _loop;
    //OStatSet ostats;
    std::shared_ptr<InfluxStream> output;
    std::map<std::chrono::nanoseconds, PeriodicStatCall> statcalls;
};

#endif  //!__STAT_IMPL_H__