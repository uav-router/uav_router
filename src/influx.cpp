#include "log.h"
#include "measure.h"
#include "udp.h"
#include "influx.h"
#include <memory>
#include <sstream>
#include <deque>

class InfluxOStatImpl : public InfluxOStat {
public:
    InfluxOStatImpl():_udp(UdpClient::create("influx_udp")) {}
    void init(  const std::string& host, 
                uint16_t port, 
                IOLoop* loop,
                std::string global_tags,
                int pack_size,
                int queue_max_size,
                int queue_shrink_size
                ) override {
        _udp->init(host,port,loop);
        _udp->on_error([](const error_c& ec) {
            log::error()<<"Influx socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
        });
        _udp->on_write_allowed([this](){ flush(); });
        if (!global_tags.empty()) {
            if (global_tags[0]!=',') { _global_tags = ","+global_tags;
            } else { _global_tags = global_tags;
            }
        }
        _pack_size = pack_size;
        _queue_max_size = queue_max_size;
        _queue_shrink_size = queue_shrink_size;
    }
    void send(Metric&& metric) override {
        int ps = _pack.tellp();
        if (ps > _pack_size) {
            if (_queue.size() > _queue_max_size) _queue.resize(_queue_shrink_size);
            std::stringstream out;
            metric.to_stream(_pack,_global_tags);
            _queue.push_front(out.str());
            return;
        }
        metric.to_stream(_pack,_global_tags);
        _pack<<'\n';
        ps = _pack.tellp();
        if (ps > _pack_size) {
            auto str = std::move(_pack.rdbuf()->str());
            int ret = _udp->write(str.c_str(),ps);
            if (ret==ps) { _pack.str("");
            }
        }
    }

    void flush() override {
        int ps = _pack.tellp();
        while(ps) {
            auto str = std::move(_pack.rdbuf()->str());
            int ret = _udp->write(str.c_str(),ps);
            if (ret==ps) { 
                _pack.str("");
                ps = 0;
            } else break;
            while(_queue.size()) {
                _pack<<_queue.back()<<'\n';
                _queue.pop_back();
                ps = _pack.tellp();
                if (ps > _pack_size) break;
            }
        }
    }
    
    int _pack_size = 400;
    int _queue_max_size = 10000;
    int _queue_shrink_size = 9900;
    std::string _global_tags;
    std::unique_ptr<UdpClient> _udp;
    std::stringstream _pack;
    std::deque<std::string> _queue;
};

auto InfluxOStat::create() -> std::unique_ptr<InfluxOStat> {
    return std::unique_ptr<InfluxOStat>{new InfluxOStatImpl()};
}