#pragma once
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/properties/qtpropertyreport.h"

// Inspect the public source collector without invoking an OS property sheet.
inline void CollectPropertyProjection(const CPanel &panel, const CRecordVector<UInt32> &indices,
                                      CListViewDialog &view) {
    QtPropertyReport report;
    panel.Properties(indices, report);
    static_cast<QtReportData &>(view) = std::move(report);
}

class PropertyProjection final : public CListViewDialog {
  public:
    PropertyProjection(const CPanel &panel, const CRecordVector<UInt32> &indices)
        : CListViewDialog() {
        CollectPropertyProjection(panel, indices, *this);
        OnInit();
        setObjectName("selectedPropertiesDialog");
    }
};
