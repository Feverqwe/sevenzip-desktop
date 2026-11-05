#pragma once

#include "Common/MyString.h"

enum class QtAddressSegmentKind { Root, Folder, Archive, Virtual };
struct QtAddressSegment {
    UString label;
    UString target;
    int iconIndex;
    QtAddressSegmentKind kind;
    bool operator==(const QtAddressSegment &) const = default;
};
