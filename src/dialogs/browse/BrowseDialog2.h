// Adapted from 7-Zip BrowseDialog2.cpp/.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#ifndef ZIP7_INC_BROWSE_DIALOG2_H
#define ZIP7_INC_BROWSE_DIALOG2_H
#include "dialogs/browse/browseiteminspection.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QDialog>
#include <functional>
#include <memory>
#include <optional>

class QTreeWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QDialogButtonBox;
class QKeyEvent;
class QUrl;

class CBrowseDialog2 : public QDialog {
    struct OwnedItem : CBrowseItem {
        QtOwnedTempEntry Owner;
    };
    struct Snapshot {
        CObjectVector<OwnedItem> Items;
        CObjectVector<NWindows::NFile::NFind::CFileInfo> Files;
        QtOwnedTempEntry Root;
        UString Prefix;
    };
    std::function<bool(const QUrl &)> _opener;
    using Details = std::function<bool(const QtOwnedTempEntry &, const QString &, bool)>;
    Details _details;
    std::shared_ptr<const Snapshot> _snapshot = std::make_shared<Snapshot>();
    QTreeWidget *_list = nullptr;
    QLineEdit *_pathEdit = nullptr;
    QPushButton *_delete = nullptr, *_parent = nullptr;
    QPushButton *_sessionDetails = nullptr;
    QLabel *_scopeLabel = nullptr;
    int _sortIndex = 1;
    bool _ascending = true;
    bool _showDots = false;
    UString _topDirPrefix;
    void BuildControls();
    void InitializeColumns();
    void ConnectControls(QDialogButtonBox *buttons, QPushButton *refresh);
    void PopulateRows(const UStringVector &selected, const UString &focused);
    void ReloadLanguagePresentation();
    void changeEvent(QEvent *event) override;
    void UpdateDeleteAvailability();
    bool IsOwnedPath(const QtOwnedTempEntry &owner, const QString &path) const;
    int GetRealItemIndex(int row) const;
    void GetSelected_RealIndexes(CUIntVector &vector);
    bool GetParentPath(const UString &path, UString &parentPrefix, UString &name);
    HRESULT Reload(const UString &pathPrefix, const UStringVector &selected,
                   const UString &focused);
    HRESULT Reload(const UString &pathPrefix, const UString &selected);
    HRESULT Reload();
    void Reload_WithErrorMessage();
    void ChangeSorting_and_Reload(int columnIndex);
    void OpenParentFolder();
    std::optional<UString> PrintFileProps(const NWindows::NFile::NFind::CFileInfo &file);
    void Show_FileProps_Window(const NWindows::NFile::NFind::CFileInfo &file);
    void OnItemEnter();
    void OnOK();
    void OnFocusChanged(QWidget *previous, QWidget *current);
    bool OnKeyDown(QKeyEvent *keyDownInfo);
    void OnContextMenu(const QPoint &position);
    void OpenOutside(const UString &path, const QtOwnedTempEntry &owner, bool sevenzip);
    void OnDelete();
    void OnDetails();
    bool eventFilter(QObject *object, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  public:
    UString TempFolderPath, Title;
    explicit CBrowseDialog2(
        QWidget *parent = nullptr, std::function<bool(const QUrl &)> opener = {},
        std::function<bool(const QtOwnedTempEntry &, const QString &, bool)> details = {});
    ~CBrowseDialog2() override;
    bool IsExactTempFolder(const UString &pathPrefix) const;
    int CompareItems(int index1, int index2) const;
    int Create();
    void accept() override;
};

void MyBrowseForTempFolder(
    QWidget *owner, std::function<bool(const QUrl &)> opener = {},
    std::function<bool(const QtOwnedTempEntry &, const QString &, bool)> details = {});
#endif
