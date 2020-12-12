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


#include <chrono>
using namespace std::chrono_literals;

#include "err.h"
#include "log.h"
#include "epoll.h"
#include "uart.h"
#include "timer.h"

namespace asm_termios {
    #include <asm/termios.h>
    struct Termios2 : public termios2 {
    public:
        Termios2(int fd):_fd(fd) {}
        error_c get() {
            return err_chk(ioctl(_fd, TCGETS2, this),"termios2 get");
        }
        error_c set() {
            return err_chk(ioctl(_fd, TCGETS2, this),"termios2 get");
        }
        int _fd;
    };

    error_c init(int fd) {
        Termios2 tc(fd);
        error_c ret = tc.get();
        if (ret) return ret;
        tc.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
        tc.c_oflag &= ~(OCRNL | ONLCR | ONLRET | ONOCR | OFILL | OPOST);
        tc.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | ECHONL | ICANON 
                             | IEXTEN | ISIG | TOSTOP);
        tc.c_cflag &= ~(CRTSCTS | CSIZE | PARENB);
        tc.c_cflag |= CLOCAL | CS8;
        tc.c_cc[VMIN] = 0;
        tc.c_cc[VTIME] = 0;
        return tc.set();
    }

    error_c set_speed(int fd, speed_t baudrate) {
        Termios2 tc(fd);
        error_c ret = tc.get();
        if (ret) return ret;
        tc.c_cflag &= ~CBAUD;
        tc.c_cflag |= BOTHER;
        tc.c_ispeed = baudrate;
        tc.c_ospeed = baudrate;
        ret = tc.set();
        if (ret) return ret;
        return err_chk(ioctl(fd, TCFLSH, TCIOFLUSH),"terminal flush");
    }

    error_c set_flow_control(int fd, bool enabled)
    {
        Termios2 tc(fd);
        error_c ret = tc.get();
        if (ret) return ret;
        if (enabled)
            tc.c_cflag |= CRTSCTS;
        else
            tc.c_cflag &= ~CRTSCTS;
        return tc.set();
    }


}

error_c reset_uart(int fd) {
    struct termios tc = {};

    error_c ret = err_chk(tcgetattr(fd, &tc),"tcgetaddr");
    if (ret) return ret;

    tc.c_cflag = CREAD;

    tc.c_iflag |= BRKINT | ICRNL | IMAXBEL;
    tc.c_iflag &= ~(INLCR | IGNCR | IUTF8 | IXOFF| IUCLC | IXANY);

    tc.c_oflag |= OPOST | ONLCR;
    tc.c_oflag &= ~(OLCUC | OCRNL | ONLRET | OFILL | OFDEL | NL0 | CR0 | TAB0 | BS0 | VT0 | FF0);

    tc.c_lflag |= ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    tc.c_lflag &= ~(ECHONL | NOFLSH | XCASE | TOSTOP | ECHOPRT);

    const cc_t default_cc[] = { 03, 034, 0177, 025, 04, 0, 0, 0, 021, 023, 032, 0,
                            022, 017, 027, 026, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, 0 };
    static_assert(sizeof(default_cc) == sizeof(tc.c_cc), "Unknown termios struct with different size");
    memcpy(tc.c_cc, default_cc, sizeof(default_cc));
    cfsetspeed(&tc, B1200);
    return err_chk(tcsetattr(fd, TCSANOW, &tc),"tcsetaddr");
}

int _set_flow_control(int baudrate) { return 0;}

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
        ret = _loop->add(_fd, EPOLLIN | EPOLLOUT, this);
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
        error_c ret = reset_uart(_fd);
        if (ret) return ret;
        ret = asm_termios::init(_fd);
        if (ret) return ret;

        struct serial_struct serial_ctl;
        ret = err_chk(ioctl(_fd, TIOCGSERIAL, &serial_ctl),"get serial "+_path);
        if (!ret) {
            serial_ctl.flags |= ASYNC_LOW_LATENCY;
            ret = err_chk(ioctl(_fd, TIOCSSERIAL, &serial_ctl),"set serial "+_path);
        }
        if (ret) {
            log::warning()<<"Low latency "<<ret.place()<<" : "<<ret.message()<<std::endl;
        }

        const int bit_dtr = TIOCM_DTR;
        const int bit_rts = TIOCM_RTS;
        ret = err_chk(ioctl(_fd, TIOCMBIS, &bit_dtr),"set dtr "+_path);
        if (ret) return ret;
        ret = err_chk(ioctl(_fd, TIOCMBIS, &bit_rts),"set rts "+_path);
        if (ret) return ret;
        ret = err_chk(ioctl(_fd, TCFLSH, TCIOFLUSH),"flush "+_path);
        if (ret) return ret;
        ret = asm_termios::set_speed(_fd, _baudrate);
        if (ret) return ret;
        ret = asm_termios::set_flow_control(_fd, _flow_control);
        if (ret) return ret;
        log::info()<<"Open UART "<<_path<<std::endl;
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
            }
            if (n==0) {
                log::warning()<<"UART read returns 0 bytes"<<std::endl;
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
    int _baudrate = 115200;
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
