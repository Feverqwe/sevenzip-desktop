// Private Qt presentation boundary for the adapted native source browser.
// Adapted from 7-Zip UI/FileManager/BrowseDialog.cpp.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Windows/FileFind.h"
#include "dialogs/browse/BrowseDialog.h"
#include "filemanager/SysIconUtils.h"
#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QComboBox;
class QLabel;
class QDialogButtonBox;
class QPushButton;
class QKeyEvent;

namespace NSourceBrowse {
class CBrowseDialog : public QDialog {
    static constexpr int kParentIndex = -1;
    QTreeWidget *_list = nullptr;
    QLineEdit *_pathEdit = nullptr;
    QComboBox *_filterCombo = nullptr;
    QLabel *_folder = nullptr;
    CObjectVector<NWindows::NFile::NFind::CFileInfo> _files;
    CExtToIconMap _extToIconMap;
    int _sortIndex = 0;
    bool _ascending = true;
    bool _showDots = false;
    UString _topDirPrefix;
    UString DirPrefix;

    bool GetParentPath(const UString &path, UString &parentPrefix, UString &name);
    HRESULT Reload(UString pathPrefix, const UString &selectedName);
    HRESULT Reload();
    void OpenParentFolder();
    void SetPathEditText();
    void OnCreateDir();
    void OnItemEnter();
    void FinishOnOK();
    void OnOK();
    bool OnButtonClicked(unsigned buttonID);
    bool OnKeyDown(QKeyEvent *keyDownInfo);
    void OnInit();
    void SetFolderText();
    void PopulateRows(const UString &selectedName);
    struct Controls {
        QDialogButtonBox *buttons;
        QPushButton *parent;
        QPushButton *create;
    };
    Controls CreateControls();
    void ConnectControls(const Controls &controls);
    void CompletePresentation(const Controls &controls);
    int GetRealItemIndex(QTreeWidgetItem *item) const;
    QTreeWidgetItem *SelectedItem() const;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

  public:
    bool SaveMode = false;
    bool FolderMode = false;
    bool StartInDirectory = false;
    int FilterIndex = -1;
    CObjectVector<CBrowseFilterInfo> Filters;
    UString FilePath;
    UString Title;
    explicit CBrowseDialog(QWidget *parent) : QDialog(parent) {}
    int Create() {
        OnInit();
        return exec();
    }
    void accept() override { FinishOnOK(); }
    int CompareItems(int lParam1, int lParam2) const;
};

} // namespace NSourceBrowse
