#pragma once

#include <stdint.h>

#include "ff.h"

#if FF_USE_LFN
const size_t c_fileNameLength = FF_LFN_BUF;
#else
const size_t c_fileNameLength = 12 + 1;
#endif

class FileSystem {
  public:
    enum Enum { IDLE, GETDIRECTORY };

    struct Status {
        uint8_t currentState;
        uint16_t totalItems;
        char cwd[c_fileNameLength];
    };

    void init();

  private:
    DIR m_dirObj;
    int m_fileCount = -1;
    char m_cwdbuf[c_fileNameLength] = "/";

    Status m_status;
};