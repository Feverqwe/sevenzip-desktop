#pragma once
// Report fields extracted from 7-Zip ListViewDialog.h; Qt language metadata is
// project integration. LGPL-2.1-or-later; third_party/7zip/License.txt.
#include "Common/MyString.h"
#include <QByteArray>
#include <QHash>
#include <optional>
#include <variant>

// Full report values and stable language bindings, independent of a widget.
class QtReportData {
  public:
    UString Title;
    UStringVector Strings, Values;
    unsigned NumColumns = 1;
    std::optional<UInt32> TitleID;
    void QtBindPropertyName(unsigned row, UInt32 id, const wchar_t *fallback);
    void QtBindDiagnosticRow(unsigned row, const char *name, const char *value);
    void QtBindArchiveDiagnosticValue(unsigned row, const UString &source);
    void QtBindArchiveFlagsValue(unsigned row, UInt32 flags);
    void RefreshRow(unsigned row, std::optional<unsigned> bindingRow = {});
    std::optional<UInt32> PropertyID(unsigned row) const;

  protected:
    struct CPropertyNameBinding {
        UInt32 ID;
        std::optional<UString> Fallback;
    };
    struct CRowBinding {
        std::optional<CPropertyNameBinding> Property;
        QPair<QByteArray, QByteArray> Diagnostic;
        std::variant<std::monostate, UString, UInt32> ArchiveValue;
    };
    QHash<unsigned, CRowBinding> _rowBindings;
};
