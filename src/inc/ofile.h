#ifndef __OFILE_INC_H__
#define __OFILE_INC_H__
#include "../err.h"
#include "../inc/endpoints.h"

class OFileStream : public Endpoint {
public:
    virtual auto open(const std::string& filename) -> error_c = 0;
};

#endif  //!__OFILE_INC_H__