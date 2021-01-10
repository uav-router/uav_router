#include "influx.h"
#include "udp.h"
#include <memory>
#include <sstream>
#include <deque>
/*
class InfluxOStatImpl : public InfluxOStat {
public:
    InfluxOStatImpl():udp(UdpClient::create("influx_udp")) {}
    void init(const std::string& host, uint16_t port, IOLoop* loop) override {

    }

    void send(Measure&& metric) override {
        int ps = pack.tellp();
        if (ps > pack_size) {
            if (queue.size() > queue_max_size) queue.resize(queue_shrink_size);
            queue.push_front(metric);
            return;
        }
        metric.to_stream(pack,global_tags);
        pack<<'\n';
        ps = pack.tellp();
        if (ps > pack_size) {
            auto str = std::move(pack.rdbuf()->str());
            int ret = udp->write(str.c_str(),ps);
            if (ret==ps) { pack.str("");
            }
        }
    }

    void flush() override {
        int ps = pack.tellp();
        while(ps) {
            auto str = std::move(pack.rdbuf()->str());
            int ret = udp->write(str.c_str(),ps);
            if (ret==ps) { pack.str("");
            } else break;
            while(queue.size()) {
                queue.back().to_stream(pack,global_tags);
                pack<<'\n';
                ps = pack.tellp();
                if (ps > pack_size) break;
            }
        }
    }
    
    int pack_size = 400;
    int queue_max_size = 10000;
    int queue_shrink_size = 9900;
    std::string global_tags;
    std::unique_ptr<UdpClient> udp;
    std::stringstream pack;
    std::deque<Measure> queue;
};

auto InfluxOStat::create() -> std::unique_ptr<InfluxOStat> {
    std::unique_ptr<InfluxOStat>{new InfluxOStatImpl{}};
}*/