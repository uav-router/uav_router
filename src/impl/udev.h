#ifndef __UDEV_IMPL__H__
#define __UDEV_IMPL__H__

#include <unistd.h>
#include <set>
#include <sys/epoll.h>
#include <libudev.h> //dnf install systemd-devel; apt-get install libudev-dev
#include "../loop.h"

class UDevIO : public UdevLoop, public IOPollable, public error_handler {
public:
    using OnActionFunc = std::function<void(udev_device*)>;
    UDevIO(IOLoopSvc* loop):IOPollable("udev"),_poll(loop->poll()) {
        _udev = udev_new();
        if (!_udev) {
            _ec = errno_c("udev_new");
            return;
        }
        _mon = udev_monitor_new_from_netlink(_udev, "udev");
        if (!_mon) {
            _ec = errno_c("udev_monitor_new_from_netlink");
            return;
        }
        int ret = udev_monitor_filter_add_match_subsystem_devtype(_mon,"tty",nullptr);
        if (ret<0) {
            _ec = errno_c("udev_monitor_filter_add_match_subsystem_devtype");
            return;
        }
        ret = udev_monitor_enable_receiving(_mon);
        if (ret<0) {
            _ec = errno_c("udev_monitor_enable_receiving");
            return;
        }
        _fd = udev_monitor_get_fd(_mon);
        if (_fd<0) {
            _ec = errno_c("udev_monitor_get_fd");
            return;
        }
        _ec = _poll->add(_fd, EPOLLIN, this);
    }
    auto get_ec() -> error_c& { return _ec; }

    void on_action(udev_device* dev) {
        std::string action = udev_device_get_action(dev);
        std::string node = udev_device_get_devnode(dev);
        //_impl->ostats.send(Metric("udev").field("dev",1).tag("action",action).tag("node",node));
        std::string link;
        udev_list_entry *list_entry;
        udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
            std::string l = udev_list_entry_get_name(list_entry);
            if (l.empty()) continue;
            if (l.rfind("/dev/serial/by-id/",0)==0) {
                link=l;
            }
        }
        if (link.empty()) return;
        for(auto& obj: udev_watches) {
            if (action=="add") {
                obj->udev_add(node,link);
            } else {
                obj->udev_remove(node,link);
            }
        }
    }

    void cleanup() override {
        if (_udev) udev_unref(_udev);
        if (_mon) udev_monitor_unref(_mon);
        if (_fd >= 0) close(_fd);
    }
    auto epollIN() -> int override {
        udev_device *dev = udev_monitor_receive_device(_mon);
        if (!dev) {
            errno_c err("udev_monitor_receive_device");
            on_error(err);
        } else on_action(dev);
        return HANDLED;
    }
    auto find_link(const std::string& path) -> std::string {
        std::string ret;
        udev_enumerate *enumerate = udev_enumerate_new(_udev);
        if (!enumerate) return ret;
        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);
        udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry *dev_list_entry;
        bool device_found = false;
        udev_list_entry_foreach(dev_list_entry, devices) {
            udev_device *dev = udev_device_new_from_syspath(_udev, udev_list_entry_get_name(dev_list_entry));
            struct udev_list_entry *list_entry;
            std::string dev_id;
            udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
                std::string link = udev_list_entry_get_name(list_entry);
                if (link.rfind("/dev/serial/by-id/",0)==0) {
                    dev_id = link;
                }
                //std::filesystem::path link = udev_list_entry_get_name(list_entry);
                //if (link.parent_path()=="/dev/serial/by-id/") {
                //    dev_id = link;
                //}

                if (!device_found) device_found = link==path;
            }
            if (device_found) {
                ret = dev_id;
                break;
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumerate);
        return ret;
    }
    auto return_id(udev_device *dev) -> std::string {
        std::string ret;
        udev_list_entry *list_entry;
        udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
            std::string link = udev_list_entry_get_name(list_entry);
            if (link.rfind("/dev/serial/by-id/",0)==0) {
                ret = link;
            }
        }
        return ret;
    }
    auto find_id(const std::string& path) -> std::string override {
        std::string sysname = path;
        auto pos = sysname.rfind("/");
        if (pos!= std::string::npos) {
            sysname = sysname.substr(pos+1);
        }
        udev_device *dev = udev_device_new_from_subsystem_sysname(_udev,"tty",sysname.c_str());
        if (!dev) return find_link(path);
        return return_id(dev);
    }

    auto find_path(const std::string& id) -> std::string override {
        std::string ret;
        udev_enumerate *enumerate = udev_enumerate_new(_udev);
        if (!enumerate) return ret;
        udev_enumerate_add_match_subsystem(enumerate, "tty");
        udev_enumerate_scan_devices(enumerate);
        udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
        udev_list_entry *dev_list_entry;
        bool device_found = false;
        udev_list_entry_foreach(dev_list_entry, devices) {
            udev_device *dev = udev_device_new_from_syspath(_udev, udev_list_entry_get_name(dev_list_entry));
            struct udev_list_entry *list_entry;
            std::string dev_id;
            udev_list_entry_foreach(list_entry, udev_device_get_devlinks_list_entry(dev)) {
                std::string link = udev_list_entry_get_name(list_entry);
                if (!device_found) device_found = link==id;
                if (device_found) break;
            }
            if (device_found) {
                ret = udev_device_get_devnode(dev);
                break;
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumerate);
        return ret;
    }

    void start_watch(UdevPollable* obj) override {
        udev_watches.insert(obj);
    }
    void stop_watch(UdevPollable* obj) override {
        udev_watches.erase(obj);
    }


private:
    udev *_udev = nullptr;
    udev_monitor *_mon = nullptr;
    int _fd = -1;
    std::set<UdevPollable*> udev_watches;
    Poll* _poll;
    error_c _ec;
};

#endif  //!__UDEV_IMPL__H__