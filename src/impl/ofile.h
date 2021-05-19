#ifndef __OFILE_IMPL_H__
#define __OFILE_IMPL_H__
#include <cstdio>
#include "../err.h"
#include "../inc/endpoints.h"
#include "../inc/ofile.h"

class OFileStreamImpl : public OFileStream {
public:
    ~OFileStreamImpl() override {
        fclose(f);
    }
    auto open(const std::string& filename) -> error_c override {
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
    auto stat() -> std::shared_ptr<Stat> override {
        return std::shared_ptr<Stat>();
    }
private:
    FILE *f;
};

#endif  //!__OFILE_IMPL_H__