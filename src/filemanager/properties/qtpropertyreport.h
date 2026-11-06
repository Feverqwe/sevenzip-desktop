#pragma once
#include "Windows/PropVariant.h"
#include "dialogs/listview/qtreportdata.h"
#include <QList>
#include <QSet>
#include <QString>

// Snapshot of the native collector. Membership is recorded while enumerating,
// never inferred from translated names or the legacy report separators.
class QtPropertyReport final : public QtReportData {
  public:
    enum class Scope { Selection, Folder, Archive, ArchiveLink, UnopenedArchive };
    enum class Availability { Known, Partial, Unknown };
    struct NativeValue {
        UInt32 id;
        VARTYPE type;
        NWindows::NCOM::CPropVariant value;
        Availability availability = Availability::Known;
        NativeValue(UInt32 id, const NWindows::NCOM::CPropVariant &source,
                    Availability availability);
    };
    struct RawValue {
        UInt32 id, type, size;
        QByteArray preview;
    };
    struct Group {
        Scope scope;
        UInt32 level;
        QList<unsigned> rows;
    };
    struct Item {
        QString name;
        std::optional<bool> directory;
        QString prefix;
    };
    QList<Group> groups;
    QList<Item> items;
    QHash<unsigned, NativeValue> nativeValues;
    QHash<unsigned, RawValue> rawValues;
    QSet<UInt32> overflowedTotals;
    QString location;
    std::optional<unsigned> selectionSummaryRow;
    void BeginGroup(Scope scope, UInt32 level = 0);
    void AddValue(const UString &name, const UString &value);
    QString Property(const Group &group, UInt32 id) const;
};
