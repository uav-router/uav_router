#ifndef __UART_IMPL_H__
#define __UART_IMPL_H__
#include <memory>
#include <unistd.h>
#include <termios.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <map>
#include <chrono>
using namespace std::chrono_literals;

#include "../err.h"
#include "../loop.h"
#include "../log.h"

#include "fd.h"
#include "statobj.h"
#include "yaml.h"

std::map<int, speed_t> bauds = {
    {0,      B0},
    {50,     B50},
    {75,     B75},
    {110,    B110},
    {134,    B134},
    {150,    B150},
    {200,    B200},
    {300,    B300},
    {600,    B600},
    {1200,   B1200},
    {1800,   B1800},
    {2400,   B2400},
    {4800,   B4800},
    {9600,   B9600},
    {19200,  B19200},
    {38400,  B38400},
    {57600,  B57600},
    {115200, B115200},
    {230400, B230400},
    {460800, B460800},
    {500000, B500000},
    {576000, B576000},
    {921600, B921600},
    {1000000,B1000000},
    {1152000,B1152000},
    {1500000,B1500000},
    {2000000,B2000000},
    {2500000,B2500000},
    {3000000,B3000000},
    {3500000,B3500000},
    {4000000,B4000000}
};

class UARTClient: public Client {
public:
    UARTClient(const std::string& name, int fd, std::shared_ptr<StatCounters>& cnt):_name(name), _fd(fd), _cnt(cnt) {}
    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) return 0;
        ssize_t n = ::write(_fd, buf, len);
        _is_writeable = n==len;
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                on_error(ret, "uart write");
            }
        } else {
            _cnt->add("write",n);
        }
        return n;
    }
    auto get_peer_name() -> const std::string& override {
        return _name;
    }

    const std::string& _name;
    int _fd;
    std::shared_ptr<StatCounters> _cnt;

    friend class UARTImpl;

};

class UARTImpl : public UART, public IOPollable, public UdevEvents {
    class UdevPollableProxy:public UdevEvents {
    public:
        UdevPollableProxy(UdevEvents* obj):_obj(obj) {}
        void udev_add(const std::string& node, const std::string& id) override {
            _obj->udev_add(node,id);
        }
        void udev_remove(const std::string& node, const std::string& id) override {
            _obj->udev_remove(node,id);
        }
    private:
        UdevEvents* _obj;
    };
public:
    UARTImpl(std::string name, IOLoopSvc* loop): IOPollable("uart"), _name(std::move(name)), _poll(loop->poll()), _udev(loop->udev()), _stat(loop->stats()) {
        _timer = loop->timer();
        _timer->shoot([this]() { init_uart_retry(); });
    }
    ~UARTImpl() override {
        if (_fd != -1) { _poll->del(_fd, this);
        }
        cleanup();
        _exists = false;
    }
    auto init(const std::string& path, int baudrate=115200, bool flow_control=false) -> error_c override {
        cnt = std::make_shared<StatCounters>(_name+"_uart_c");
        _stat->register_report(cnt, 1s);
        _path = path;
        _baudrate = baudrate;
        _flow_control = flow_control;
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _timer->on_error(on_err);
        init_uart_retry();
        return error_c();
    }

#ifdef YAML_CONFIG
    auto init(YAML::Node cfg) -> error_c override {
        if (!cfg["path"]) return errno_c(ENOTSUP,"uart device path");
        _path = cfg["path"].as<std::string>();
        _baudrate = 115200;
        if (cfg["baudrate"]) _baudrate = cfg["baudrate"].as<int>();
        _flow_control = false;
        if (cfg["flow_control"]) _flow_control = cfg["flow_control"].as<bool>();
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _timer->on_error(on_err);
        cnt = std::make_shared<StatCounters>(_name+"_uart_c");
        auto statcfg = cfg["stat"];
        if (statcfg && statcfg.IsMap()) {
            std::chrono::nanoseconds period = 1s;
            auto stat_period = duration(statcfg["period"]);
            if (stat_period.count()) { period = stat_period;
            } else {
                log.error()<<"Unreadable stat duration "<<statcfg["period"].as<std::string>()<<std::endl;
            }
            auto tags = statcfg["tags"];
            if (tags && tags.IsMap()) {
                for(auto tag : tags) {
                    std::string key = tag.first.as<std::string>();
                    std::string val = tag.second.as<std::string>();
                    cnt->tags.push_front(std::make_pair(key,val));
                }
            }
            _stat->register_report(cnt, period);
        }
        init_uart_retry();
        return error_c();
    }
#endif

    void init_uart_retry() {
        error_c ret = init_uart();
        if (on_error(ret,"init_uart")) {
            if (!_exists) return;
            if (!_usb_id.empty() && ret == std::error_condition(std::errc::no_such_device)) {
                return;
            }
            ret = _timer->arm_oneshoot(5s);
            on_error(ret,"rearm timer");
            return;
        }
        ret = _poll->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        on_error(ret,"uart loop add");
    }

    void start_udev_watch() {
        if (!_usb_id.empty() && !_udev_pollable) {
            log.debug()<<"USB port "<<_usb_id<<" detected"<<std::endl;
            _udev_pollable = std::make_shared<UdevPollableProxy>(this);
            _udev->start_watch(_udev_pollable);
        }
    }

    auto init_uart() -> error_c {
        if (_fd!=-1) { 
            close(_fd);
            _fd = -1;
        }
        FD watcher(_fd);
        if (_udev) {
            if (_usb_id.empty()) {
                if (_path.rfind("/dev/serial/by-id/",0)==0) {
                    _usb_id = _path;
                    _path.clear();
                } else {
                    _usb_id = _udev->find_id(_path);
                }
                start_udev_watch();
            }
            if (!_usb_id.empty()) {
                auto found = _udev->find_path(_usb_id);
                if (found.empty()) return errno_c(ENODEV,_usb_id+" not found");
                _path = found;
            }
        }
        _fd = ::open(_path.c_str(), O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOCTTY);
        if (_fd == -1) { return errno_c("uart "+_path+" open");
        }
        const auto& b = bauds.find(_baudrate);
        if (b == bauds.end()) return errno_c(EINVAL,"uart "+_path+" open");
        struct termios tty;
        error_c ret = err_chk(tcgetattr(_fd, &tty),"tcgetaddr");
        if (ret) return ret;
        ret = err_chk(cfsetospeed(&tty, b->second),"cfsetospeed");
        if (ret) return ret;
        ret = err_chk(cfsetispeed(&tty, b->second),"cfsetispeed");
        if (ret) return ret;
        tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8 | CLOCAL | CREAD;
        if (_flow_control) tty.c_cflag |= CRTSCTS;
        
        tty.c_iflag = IGNBRK;
        //if (software_flow) tty.c_iflag |= IXON | IXOFF;
        
        tty.c_lflag = 0;
        tty.c_oflag = 0;
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;
        ret = err_chk(tcsetattr(_fd, TCSANOW, &tty),"tcsetaddr");
        if (ret) return ret;
        
        const int bit_dtr = TIOCM_DTR;
        ret = err_chk(ioctl(_fd, TIOCMBIS, &bit_dtr),"set dtr "+_path);
        if (ret) return ret;
        
        const int bit_rts = TIOCM_RTS;
        ret = err_chk(ioctl(_fd, TIOCMBIS, &bit_rts),"set rts "+_path);
        if (ret) return ret;
        
        struct serial_struct serial_ctl;
        ret = err_chk(ioctl(_fd, TIOCGSERIAL, &serial_ctl),"get serial "+_path);
        if (!ret) {
            serial_ctl.flags |= ASYNC_LOW_LATENCY;
            ret = err_chk(ioctl(_fd, TIOCSSERIAL, &serial_ctl),"set serial "+_path);
        }
        if (ret) {
            log.warning()<<"Low latency "<<ret<<std::endl;
            log.info()<<"Open UART "<<_path<<std::endl;
        } else {
            log.info()<<"Open UART with low latency "<<_path<<std::endl;
        }
        ret = err_chk(ioctl(_fd, TCFLSH, TCIOFLUSH),"flush "+_path);
        if (ret) return ret;
        
        watcher.clear();
        return error_c();
    }

    auto epollIN() -> int override {
        int n = 1024;
        while(n==1024) {
            std::array<char,1024> buffer;
            n = read(_fd, buffer.data(), buffer.size());
            if (n == -1) {
                errno_c ret;
                if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                    on_error(ret, "uart read");
                    if (!_exists) return STOP;
                }
                break;
            }
            if (n==0) {
                log.warning()<<"UART read returns 0 bytes"<<std::endl;
                break;
            }
            cnt->add("read",n);
            if (auto client = cli()) {
                if (!_exists) return STOP;
                client->on_read(buffer.data(), n);
            }
            if (!_exists) return STOP;
        }
        return HANDLED;
    }

    auto epollOUT() -> int override {
        auto client = cli();
        return HANDLED;
    }

    auto epollERR() -> int override {
        log.debug()<<"EPOLLERR on uart "<<_path<<std::endl;
        return HANDLED;
    }

    auto epollHUP() -> int override {
        log.debug()<<"EPOLLHUP on uart "<<_path<<std::endl;
        if (_fd != -1) {
            _poll->del(_fd, this);
        }
        cleanup();
        if (!_exists) return STOP;
        if (_usb_id.empty()) init_uart_retry();
        return HANDLED;
    }

    void cleanup() override {
        if (!_client.expired()) {
            if (auto client = cli()) client->on_close();
        }
        if (_fd != -1) {
            close(_fd);
            _fd = -1;
        }
    }

    auto cli(bool writeable=true) -> std::shared_ptr<UARTClient> {
        std::shared_ptr<UARTClient> ret;
        if (!_client.expired()) { 
            ret = _client.lock();
        }
        if (!ret) {
            if (_usb_id.empty()) {
                ret = std::make_shared<UARTClient>(_path,_fd, cnt);
            } else {
                ret = std::make_shared<UARTClient>(_usb_id,_fd, cnt);
            }
            ret->on_error([this](error_c ec){on_error(ec);});
            _client = ret;
            if (writeable) ret->writeable();
            on_connect(ret, ret->get_peer_name());
        }
        return ret;
    }

    void udev_add(const std::string& node, const std::string& id) override {
        if (id==_usb_id) { 
            log.debug()<<"Device "<<_usb_id<<" added"<<std::endl;
            init_uart_retry();
        }
    }
    void udev_remove(const std::string& node, const std::string& id) override {
        if (id==_usb_id) {
            log.debug()<<"Device "<<_usb_id<<" removed"<<std::endl;
            if (_fd!=-1) { 
                _poll->del(_fd,this);
                cleanup();
            }
        }
    }



    std::string _name;
    std::string _path;
    std::string _usb_id;
    int _baudrate = 115200;
    bool _flow_control = false;

    int _fd = -1;
    std::weak_ptr<UARTClient> _client;
    std::shared_ptr<UdevEvents> _udev_pollable;
    bool _exists = true;

    Poll* _poll;
    UdevLoop* _udev;
    StatHandler* _stat;
    std::unique_ptr<Timer> _timer;
    inline static Log::Log log {"uart"};
    std::shared_ptr<StatCounters> cnt;
};

#endif  //!__UART_IMPL_H__