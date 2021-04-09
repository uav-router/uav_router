#ifndef __UDEV_INC_H__
#define __UDEV_INC_H__
#include <string>
#include <memory>

class UdevEvents {
public:
    virtual ~UdevEvents() = default;
    virtual void udev_add(const std::string& node, const std::string& id) = 0;
    virtual void udev_remove(const std::string& node, const std::string& id) = 0;
};

class UdevLoop {
public:
    virtual void start_watch(std::shared_ptr<UdevEvents>& obj) = 0;
    virtual auto find_id(const std::string& path) -> std::string = 0;
    virtual auto find_path(const std::string& id) -> std::string = 0;
};

#endif  //!__UDEV_INC_H__