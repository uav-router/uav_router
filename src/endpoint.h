#ifndef __ENDPOINT_H__
#define __ENDPOINT_H__
#include <string>
#include "err.h"

/*
               UART  TCP_SRV TCPSRV_STREAM TCP_CLI UDP_SRV UDP_SRV_STREAM UDP_CLI
on_read          X      0          X          X       0          X           X
on_write         X      0          X          X       X          0           X
on_connect       X      0          0          X       0          0           X
on_connect_cli   0      X          0          0       X          0           0
on_close         X      X          X          X       X          X           X
*/
using OnEventFunc = std::function<void()>;

class Readable : public error_handler {
public:
    using OnReadFunc  = std::function<void(void*, int)>;
    virtual void on_read(OnReadFunc func) = 0;
    virtual auto get_peer_name() -> const std::string& = 0;
};

class Writeable {
public:
    virtual ~Writeable() = default;
    virtual auto write(const void* buf, int len) -> int = 0;
    void writeable(OnEventFunc func) {_writeable = func;}
    auto is_writeable() -> bool { return _is_writeable; }
protected:
    void writeable() {if (_writeable) _writeable(); _is_writeable = true;}
    bool _is_writeable = false;
private:
    OnEventFunc _writeable;q
};



#endif //__ENDPOINT_H__

