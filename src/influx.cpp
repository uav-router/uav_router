#include "log.h"
#include "measure.h"
#include "udp.h"
#include <memory>
#include <sstream>
#include <deque>

class InfluxOStatImpl : public OStat {
public:
    InfluxOStatImpl():udp(UdpClient::create("influx_udp")) {}
    void init(const std::string& host, uint16_t port, IOLoop* loop) {
        udp->init(host,port,loop);
        udp->on_error([](const error_c& ec) {
            log::error()<<"Influx socket error:"<<ec.place()<<": "<<ec.message()<<std::endl;
        });
        udp->on_write_allowed([this](){ flush(); });
    }
    void send(Measure&& metric) override {
        int ps = pack.tellp();
        if (ps > pack_size) {
            if (queue.size() > queue_max_size) queue.resize(queue_shrink_size);
            std::stringstream out;
            metric.to_stream(pack,global_tags);
            queue.push_front(out.str());
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
                pack<<queue.back()<<'\n';
                queue.pop_back();
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
    std::deque<std::string> queue;
};

/*auto InfluxOStat::create() -> std::unique_ptr<InfluxOStat> {
    std::unique_ptr<InfluxOStat>{new InfluxOStatImpl{}};
}*/