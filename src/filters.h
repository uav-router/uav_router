#ifndef __FILTERS__H__
#define __FILTERS__H__

#include "inc/endpoints.h"
#include <memory>

namespace Filters {
    auto create(std::string name) -> std::shared_ptr<Filter>;
    void register_plugin(std::string name);
};
#endif  //!__FILTERS__H__