// TextPairs.h

#ifndef ZIP7_INC_FM_TEXT_PAIRS_H
#define ZIP7_INC_FM_TEXT_PAIRS_H

#include "Common/MyString.h"
#include "common/collections/MyVector.h"

struct CTextPair {
    UString ID;
    UString Value;
};

class CPairsStorage {
    CObjectVector<CTextPair> Pairs;

    int FindID(const UString &id, unsigned &insertPos) const;
    int FindID(const UString &id) const;
    void Sort();

  public:
    void Clear() { Pairs.Clear(); }
    bool ReadFromString(const UString &text);

    bool GetValue(const UString &id, UString &value) const;
    UString GetValue(const UString &id) const;
};

#endif
