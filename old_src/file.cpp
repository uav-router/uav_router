#include <sys/epoll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "err.h"
#include "log.h"
#include "loop.h"
#include "file.h"

class FileImpl: public OFile {
public:
    FileImpl() = default;
    ~FileImpl() override {
        if (_fd!=-1) close(_fd);
    }
    void init(const std::string& filename) override {
        _filename = filename;
        auto on_err = [this](error_c& ec){ on_error(ec,_name);};
        _fd = ::open(_filename.c_str(), O_WRONLY| O_APPEND | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (_fd==-1) {
            errno_c ret;
            on_error(ret,"file open "+_filename);
        }
    }

    auto write(const void* buf, int len) -> int override {
        ssize_t n = ::write(_fd, buf, len);
        if (n==-1) {
            errno_c ret;
            if (ret != std::error_condition(std::errc::resource_unavailable_try_again)) {
                on_error(ret, "file write "+_filename);
            }
        }
        return n;
    }
    
private:
    std::string _name;
    std::string _filename;
    int _fd = -1;
};

auto OFile::create() -> std::unique_ptr<OFile> {
    return std::unique_ptr<OFile>{new FileImpl()};
}
