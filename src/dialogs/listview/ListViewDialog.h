// Adapted from 7-Zip CPP/7zip/UI/FileManager/ListViewDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "Common/MyString.h"
#include <QByteArray>
#include <QDialog>
#include <QHash>
#include <optional>
#include <variant>

class CListView;
class CListViewDialog : public QDialog {
    Q_OBJECT
    static constexpr int kRowIdentityRole = Qt::UserRole + 14;
    CListView *_listView = nullptr;
    void OnOK();
    void CopyToClipboard(bool all = false);
    void DeleteItems();
    void ShowItemInfo();
    void OnEnter();
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    void changeEvent(QEvent *event) override;

  public:
    explicit CListViewDialog(QWidget *parent = nullptr);
    bool OnInit();
    void QtBindPropertyName(unsigned row, UInt32 id, const wchar_t *fallback);
    void QtBindDiagnosticRow(unsigned row, const char *name, const char *value);
    void QtBindArchiveDiagnosticValue(unsigned row, const UString &source);
    void QtBindArchiveFlagsValue(unsigned row, UInt32 flags);
    void accept() override;
    UString Title;
    QString SettingsPath;
    bool SelectFirst = false;
    bool DeleteIsAllowed = false;
    bool StringsWereChanged = false;
    UStringVector Strings;
    UStringVector Values;
    unsigned NumColumns = 1;
    int FocusedItemIndex = -1;
};
