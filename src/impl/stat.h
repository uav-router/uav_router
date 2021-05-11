#ifndef __STAT_IMPL_H__
#define __STAT_IMPL_H__
#include <forward_list>
#include <map>
#include <memory>
#include "../loop.h"
#include "influx.h"

class OStatSet : public OStat {
public:
    void send(Metric&& metric) override {
        for(auto& ostat:ostats) ostat->send(std::forward<Metric>(metric));
    }
    void flush() override {
        for(auto& ostat:ostats) ostat->flush();
    }
    std::forward_list<std::unique_ptr<OStat>> ostats;
};

class PeriodicStatCall {
public:
    void init(std::unique_ptr<Timer> timer, std::chrono::nanoseconds period, OStat* sink) {
        _timer = std::move(timer);
        _period = period;
        _sink = sink;
        _timer->shoot([this](){
            auto it = _stats.before_begin();
            for(auto p = _stats.begin();p!=_stats.end();p=std::next(it)) {
                if (p->expired()) {
                    p = _stats.erase_after(it);
                    continue;
                }
                p->lock()->report(*_sink);
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
    OStat* _sink;
    std::forward_list<std::weak_ptr<Stat>> _stats;
};

class StatHandlerImpl : public StatHandler, public error_handler {
public:
    StatHandlerImpl(IOLoop* loop):_loop(loop) {}
    void add_output(std::unique_ptr<OStat> out) override {
        bool ostats_empty = ostats.ostats.empty();
        ostats.ostats.push_front(std::move(out));
        if (ostats_empty && !statcalls.empty()) {
            for(auto& statcall: statcalls) { statcall.second.start();
            }
        }
    }
    void clear_outputs() override {
        for(auto& statcall: statcalls) {
            statcall.second.stop();
        }
        ostats.ostats.clear();
    }
    auto influx_udp(const std::string& host, uint16_t port) -> std::unique_ptr<OStatEndpoint> override {
        auto udp = _loop->udp_client("influx");
        udp->on_error([this](const error_c& ec) {on_error(ec);});
        udp->init(host,port);
        auto ret = std::make_unique<InfluxStream>();
        ret->init(std::unique_ptr<Writeable>(udp.release()));
        return ret;
    }
    auto influx_line(const std::string& filename) -> std::unique_ptr<OStatEndpoint> override {
        auto stream = std::make_unique<OFileStream>();
        error_c ec = stream->open(filename);
        if (ec) { 
            on_error(ec);
            return std::unique_ptr<OStatEndpoint>();
        }
        stream->on_error([this](const error_c& ec) {on_error(ec);});
        auto ret = std::make_unique<InfluxStream>();
        ret->init(std::unique_ptr<Writeable>(stream.release()));
        return ret;
    }
    void register_report(std::shared_ptr<Stat> source, std::chrono::nanoseconds period) {
        auto it = statcalls.find(period);
        bool not_found = it==statcalls.end();
        auto& statcall = statcalls[period];
        if (not_found) {
            auto timer = _loop->timer();
            timer->on_error([this](const error_c& ec) {on_error(ec);});
            statcall.init(std::move(timer),period,&ostats);
        }
        statcall.add(source);
    }
private:
    IOLoop* _loop;
    OStatSet ostats;
    std::map<std::chrono::nanoseconds, PeriodicStatCall> statcalls;
};

#endif  //!__STAT_IMPL_H__