#ifndef __FD__H__
#define __FD__H__
#include <unistd.h>

struct FD {
    int* _fd;
    FD(int& fd): _fd(&fd) {}
    ~FD() {
        if (_fd && *_fd!=-1) {
            close(*_fd);
            *_fd = -1;
        }
    }
    void clear() {_fd=nullptr;}
};

#endif  //!__FD__H__