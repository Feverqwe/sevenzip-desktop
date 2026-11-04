// Extracted from 7-Zip CPP/7zip/UI/FileManager/Panel.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "C/Alloc.h"

class CMyBuffer {
    void *_data;

  public:
    CMyBuffer() : _data(NULL) {}
    operator void *() { return _data; }
    bool Allocate(size_t size) {
        if (_data)
            return false;
        _data = ::MidAlloc(size);
        return _data != NULL;
    }
    ~CMyBuffer() { ::MidFree(_data); }
};
