// Adapted from 7-Zip CPP/7zip/UI/FileManager/ListViewDialog.h (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once

#include "dialogs/listview/qtreportdata.h"
#include <QDialog>

class CListView;
class CListViewDialog : public QDialog, public QtReportData {
    Q_OBJECT
    static constexpr int kRowIdentityRole = Qt::UserRole + 14;
    CListView *_listView = nullptr;
    void OnOK();
    void CopyToClipboard(bool all = false);
    void DeleteItems();
    void ShowItemInfo();
    void OnEnter();
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

  public:
    explicit CListViewDialog(QWidget *parent = nullptr);
    bool OnInit();
    void accept() override;
    QString SettingsPath;
    bool SelectFirst = false;
    bool DeleteIsAllowed = false;
    bool StringsWereChanged = false;
    int FocusedItemIndex = -1;
};
