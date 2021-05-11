#ifndef __INFLUX__H__
#define __INFLUX__H__
#include "../inc/stat.h"
#include "../inc/endpoints.h"
#include "../ioloop.h"
#include <deque>
#include <cstdio>


class InfluxStream : public OStatEndpoint {
public:
    void pack_size(int size) override { _pack_size = size;
    }
    void queue_size(int max, int shrink) override {
        _queue_max_size = max;
        _queue_shrink_size = shrink;
    }
    void global_tags(std::string values) override {
        if (!values.empty()) {
            if (values[0]!=',') { _global_tags = ","+values;
            } else { _global_tags = values;
            }
        }
    }
    void init( std::unique_ptr<Writeable> out ) {
        _out = std::move(out);
        _out->writeable([this](){ flush(); });
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
            int ret = _out->write(str.c_str(),ps);
            if (ret==ps) { _pack.str("");
            }
        }
    }
    void flush() override {
        int ps = _pack.tellp();
        while(ps) {
            auto str = std::move(_pack.rdbuf()->str());
            int ret = _out->write(str.c_str(),ps);
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
    std::unique_ptr<Writeable> _out;
    std::stringstream _pack;
    std::deque<std::string> _queue;
};

class OFileStream : public Writeable, public error_handler {
public:
    ~OFileStream() override {
        fclose(f);
    }
    auto open(const std::string& filename) -> error_c{
        f = fopen(filename.c_str(),"a");
        if (!f) return errno_c("fopen stream");
        return error_c();
    }
    auto write(const void* buf, int len) -> int override {
        size_t ret =  fwrite(buf,1,len,f);
        if (ferror(f)) {
            on_error(errno_c("write file"));
        }
        return ret;
    }
private:
    FILE *f;
};

#endif  //!__INFLUX__H__