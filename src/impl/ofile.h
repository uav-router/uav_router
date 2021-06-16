#ifndef __OFILE_IMPL_H__
#define __OFILE_IMPL_H__
#include <cstdio>
#include "../err.h"
#include "../inc/endpoints.h"
#include "../inc/ofile.h"

class OFileStreamImpl : public OFileStream {
public:
    ~OFileStreamImpl() override {
        if (f) { 
            if (f==stdout) return;
            if (f==stderr) return;
            fclose(f);
        }
    }
    auto open(const std::string& filename) -> error_c override {
        if (filename=="stdout") {
            f = stdout;
        } else if (filename=="stderr") {
            f = stderr;
        } else {
            f = fopen(filename.c_str(),"a");
        }
        if (!f) return errno_c("fopen stream");
        return error_c();
    }
    auto write(const void* buf, int len) -> int override {
        if (prefix.size()) fwrite(prefix.data(),1,prefix.size(),f);
        size_t ret =  fwrite(buf,1,len,f);
        if (suffix.size()) fwrite(suffix.data(),1,suffix.size(),f);
        if (ferror(f)) {
            on_error(errno_c("write file"));
        }
        return ret;
    }
    auto stat() -> std::shared_ptr<Stat> override {
        return std::shared_ptr<Stat>();
    }
#ifdef YAML_CONFIG
    auto init_yaml(YAML::Node cfg) -> error_c override {
        auto color = cfg["color"];
        if (color && color.IsScalar()) {
            std::map<std::string,std::string> colors = {
                {"black", "\033[30m"},
                {"red", "\033[31m"},
                {"green", "\033[32m"},
                {"yellow", "\033[33m"},
                {"blue", "\033[34m"},
                {"magenta", "\033[35m"},
                {"cyan", "\033[36m"},
                {"lightgray", "\033[37m"},
                {"gray", "\033[90m"},
                {"lightred", "\033[91m"},
                {"lightgreen", "\033[92m"},
                {"lightyellow", "\033[93m"},
                {"lightblue", "\033[94m"},
                {"lightmagenta", "\033[95m"},
                {"lightcyan", "\033[96m"},
                {"white", "\033[97m"}
            };
            auto c = colors.find(color.as<std::string>());
            if (c!=colors.end()) {
                prefix = c->second;
                suffix = "\033[0m";
            }
        }
        auto name = cfg["name"];
        if (name && name.IsScalar()) {
            return open(name.as<std::string>());
        }
        return error_c(ENOENT);
    }
#endif //YAML_CONFIG

private:
    std::string prefix;
    std::string suffix;
    FILE *f;
};

#endif  //!__OFILE_IMPL_H__