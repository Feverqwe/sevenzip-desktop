#include "filemanager/properties/qtpropertyreport.h"
#include "Common/StringConvert.h"
#include "settings/ViewSettings.h"

QtPropertyReport::NativeValue::NativeValue(UInt32 property,
                                           const NWindows::NCOM::CPropVariant &source,
                                           Availability state)
    : id(property), type(source.vt), availability(state) {
    // Retain only the owned scalar/string/time values used by the native
    // formatter. Unsupported types keep its formatted marker and type tag,
    // without copying an interface or a borrowed reference into this snapshot.
    switch (source.vt) {
    case VT_EMPTY:
    case VT_BSTR:
    case VT_BOOL:
    case VT_FILETIME:
    case VT_UI1:
    case VT_UI2:
    case VT_UI4:
    case VT_UI8:
    case VT_I2:
    case VT_I4:
    case VT_I8:
        value = source;
        break;
    default:
        break;
    }
}

void QtPropertyReport::BeginGroup(Scope scope, UInt32 level) { groups.append({scope, level, {}}); }
void QtPropertyReport::AddValue(const UString &name, const UString &value) {
    if (!groups.isEmpty())
        groups.last().rows.append(Strings.Size());
    Strings.Add(name);
    Values.Add(value);
}
QString QtPropertyReport::Property(const Group &group, UInt32 id) const {
    for (const auto row : group.rows)
        if (PropertyID(row) == id && row < Values.Size())
            return ToQString(Values[row]);
    return {};
}
