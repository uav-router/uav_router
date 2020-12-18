#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <termios.h>
#include <linux/serial.h>

#include <map>
#include <chrono>
using namespace std::chrono_literals;

#include "err.h"
#include "log.h"
#include "epoll.h"
#include "uart.h"
#include "timer.h"

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
    UARTImpl(const std::string& name): IOPollable("uart"), _name(name) {}
    
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
            _timer.init_oneshoot(5s);
            ret = _loop->execute(&_timer);
            if (on_error(ret,"restart timer")) return;
            _timer.on_shoot_func([this]() { init_uart_retry(); });
            return;
        }
        ret = _loop->add(_fd, EPOLLIN | EPOLLOUT | EPOLLET, this);
        on_error(ret,"uart loop add");
    }
    
    struct FD {
        int _fd;
        FD(int fd=-1): _fd(fd) {}
        ~FD() {
            if (_fd!=-1) close(_fd);
        }
        void clear() {_fd=-1;}
    };

    error_c init_uart() {
        _fd = ::open(_path.c_str(), O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOCTTY);
        if (_fd == -1) { return errno_c("uart "+_path+" open");
        }
        FD watcher(_fd);
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
            log::warning()<<"Low latency "<<ret.place()<<" : "<<ret.message()<<std::endl;
            log::info()<<"Open UART "<<_path<<std::endl;
        } else {
            log::info()<<"Open UART with low latency "<<_path<<std::endl;
        }
        ret = err_chk(ioctl(_fd, TCFLSH, TCIOFLUSH),"flush "+_path);
        if (ret) return ret;
        
        watcher.clear();
        return error_c();
    }

    int epollIN() override {
        while(true) {
            char buffer[1024];
            int n = read(_fd, buffer, sizeof(buffer));
            if (n == -1) {
                errno_c ret;
                if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                    on_error(ret, "uart read");
                }
                break;
            }
            if (n==0) {
                //log::warning()<<"UART read returns 0 bytes"<<std::endl;
                break;
            }
            if (_on_read) _on_read(buffer, n);
        }
        return HANDLED;
    }

    int epollOUT() override {
        _is_writeable = true;
        return HANDLED;
    }

    error_c start_with(IOLoop* loop) override {
        _loop = loop;
        init_uart_retry();
        return error_c();
    }

    void cleanup() override {
        if (_fd != -1) {
            close(_fd);
        }
    }
    int write(const void* buf, int len) {
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
    void on_read(OnReadFunc func) override {
        _on_read=func;
    }

    void on_connect(OnEventFunc func) override {
        _on_connect=func;
    }

    void on_close(OnEventFunc func) override {
        _on_close=func;
    }
private:
    std::string _name;
    std::string _path;
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

std::unique_ptr<UART> UART::create(const std::string& name) {
    return std::unique_ptr<UART>{new UARTImpl(name)};
}
