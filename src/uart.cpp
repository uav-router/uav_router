#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <termios.h>
#include <linux/serial.h>

#include <map>
#include <chrono>
#include <cerrno>
#include <utility>
using namespace std::chrono_literals;

#include "err.h"
#include "log.h"
#include "loop.h"
#include "uart.h"
#include "timer.h"
static Log::Log log("uart");
struct FD {
    int* _fd;
    FD(int& fd): _fd(&fd) {}
    ~FD() {
        if (_fd && *_fd!=-1) close(*_fd);
    }
    void clear() {_fd=nullptr;}
};

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


class UARTImpl: public IOPollable, public UART {
public:
    UARTImpl(std::string name): IOPollable("uart"), _name(std::move(name)) {}
    
    void init(const std::string& path, IOLoop* loop, int baudrate, bool flow_control) override {
        _path = path;
        _baudrate = baudrate;
        _flow_control = flow_control;
        _loop = loop;
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _timer.on_error(on_err);
        init_uart_retry();
    }

    void init_uart_retry() {
        error_c ret = init_uart();
        if (on_error(ret,"init_uart")) {
            if (!_usb_id.empty() && ret == std::error_condition(std::errc::no_such_device)) return;
            ret = _loop->execute(&_timer);
            if (on_error(ret,"recreate timer")) return;
            ret = _timer.arm_oneshoot(5s);
            if (on_error(ret,"rearm timer")) return;
            _timer.on_shoot_func([this]() { init_uart_retry(); });
            return;
        }
        ret = _loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        on_error(ret,"uart loop add");
    }

    auto init_uart() -> error_c {
        if (_fd!=-1) close(_fd);
        FD watcher(_fd);
        if (_usb_id.empty()) {
            if (_path.rfind("/dev/serial/by-id/",0)==0) {
                _usb_id = _path;
                _path.clear();
            } else {
                _usb_id = _loop->udev_find_id(_path);
            }
            if (!_usb_id.empty()) {
                log.debug()<<"USB port "<<_usb_id<<" detected"<<std::endl;
                _loop->udev_start_watch(this);
            }
        }
        if (!_usb_id.empty()) {
            auto found = _loop->udev_find_path(_usb_id);
            if (found.empty()) return errno_c(ENODEV,_usb_id+" not found");
            _path = found;
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
        while(true) {
            std::array<char,1024> buffer;
            int n = read(_fd, buffer.data(), buffer.size());
            if (n == -1) {
                errno_c ret;
                if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                    on_error(ret, "uart read");
                }
                break;
            }
            if (n==0) {
                //log.warning()<<"UART read returns 0 bytes"<<std::endl;
                break;
            }
            if (_on_read) _on_read(buffer.data(), n);
        }
        return HANDLED;
    }

    auto epollOUT() -> int override {
        _is_writeable = true;
        write_allowed();
        return HANDLED;
    }

    auto epollERR() -> int override {
        log.debug()<<"EPOLLERR on uart "<<_path<<std::endl;
        return HANDLED;
    }

    auto epollHUP() -> int override {
        if (_fd != -1) {
            _loop->del(_fd, this);
            close(_fd);
            _fd = -1;
            init_uart_retry();
        }
        return HANDLED;
    }

    auto start_with(IOLoop* loop) -> error_c override {
        _loop = loop;
        init_uart_retry();
        return error_c();
    }

    void cleanup() override {
        if (_fd != -1) {
            close(_fd);
        }
        if (!_usb_id.empty()) { _loop->udev_stop_watch(this);
        }
    }
    auto write(const void* buf, int len) -> int override {
        if (!_is_writeable) return 0;
        ssize_t n = ::write(_fd, buf, len);
        _is_writeable = n==len;
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                on_error(ret, "uart write");
            }
        }
        return n;
    }
    void on_read(OnReadFunc func) override { _on_read=func;
    }

    void on_connect(OnEventFunc func) override { _on_connect=func;
    }

    void on_close(OnEventFunc func) override { _on_close=func;
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
            if (_fd != -1) { 
                _loop->del(_fd, this);
                close(_fd);
                _fd = -1;
            }
        }
    }
    
private:
    std::string _name;
    std::string _path;
    std::string _usb_id;
    speed_t _baudrate = B115200;
    bool _is_writeable = false;
    bool _flow_control = false;
    IOLoop* _loop;
    Timer _timer;
    int _fd;
    OnReadFunc _on_read;
    OnEventFunc _on_close;
    OnEventFunc _on_connect;
};

auto UART::create(const std::string& name) -> std::unique_ptr<UART> {
    return std::unique_ptr<UART>{new UARTImpl(name)};
}
