// Adapted from 7-Zip SystemPage.h (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "filemanager/providers/FilePlugins.h"
#include "platform/associations/qtassociationservices.h"
#include "settings/RegistryAssociations.h"
#include <QIcon>
#include <QList>
#include <QWidget>
#include <memory>
class QTreeWidget;
class QPushButton;
class QLabel;
class QCheckBox;
class QLineEdit;

enum EExtState { kExtState_Clear = 0, kExtState_Other, kExtState_7Zip };
struct CModifiedExtInfo : public NRegistryAssoc::CShellExtInfo {
    int OldState;
    int State;
    int ImageIndex;
    bool Other;
    bool Other7Zip;
    CModifiedExtInfo() : ImageIndex(-1) {}
    CSysString GetString() const;
    void SetState(const UString &iconPath) {
        State = kExtState_Clear;
        Other = false;
        Other7Zip = false;
        if (!ProgramKey.IsEmpty()) {
            State = kExtState_Other;
            Other = true;
            if (IsIt7Zip()) {
                Other7Zip = !iconPath.IsEqualTo_NoCase(IconPath);
                if (!Other7Zip) {
                    State = kExtState_7Zip;
                    Other = false;
                }
            }
        }
        OldState = State;
    }
};
struct CAssoc {
    CModifiedExtInfo Pair[2];
    int SevenZipImageIndex = -1;
    int GetIconIndex() const {
        for (unsigned i = 0; i < 2; i++) {
            const CModifiedExtInfo &pair = Pair[i];
            if (pair.State == kExtState_Clear)
                continue;
            if (pair.State == kExtState_7Zip)
                return SevenZipImageIndex;
            if (pair.ImageIndex != -1)
                return pair.ImageIndex;
        }
        return -1;
    }
};
class CSystemPage : public QWidget {
    Q_OBJECT
    CExtDatabase _extDB;
    CObjectVector<CAssoc> _items;
    CObjectVector<QIcon> _imageList;
    std::shared_ptr<IQtAssociationServices> _services;
    QTreeWidget *_listView;
    QPushButton *_current;
    QPushButton *_all;
    QLabel *_label;
    bool _needSave = false;
    QList<QCheckBox *> _formatChoices;
    QTreeWidget *_formatList = nullptr;
    QLineEdit *_formatSearch = nullptr;
    QLabel *_formatCount = nullptr;
    QLabel *_formatEmpty = nullptr;
    void RefreshFormatChoices();
    void FilterFormats();
    void SelectFormats(bool selected);
    void SelectFormat(unsigned index, bool selected);
    unsigned GetRealIndex(unsigned listIndex) const { return listIndex; }
    int AddIcon(const UString &path, int iconIndex, const QIcon &nativeIcon = {});
    UString OwnFormatIconPath(unsigned realIndex) const;
    void PrepareAssociationPresentation(unsigned group, unsigned realIndex, CModifiedExtInfo &info);
    void RefreshAvailability();
    void RefreshListItem(unsigned group, unsigned listIndex);
    void ChangeState(unsigned group, const CUIntVector &indices);
    void ChangeState(unsigned group);
    bool eventFilter(QObject *object, QEvent *event) override;

  public:
    bool WasChanged = false;
    explicit CSystemPage(IFolderManager *manager, std::shared_ptr<IQtAssociationServices> services,
                         QWidget *parent = nullptr);
    bool OnInit(IFolderManager *manager);
    LONG OnApply();
    void ReloadLangItems();
    bool IsModified() const { return _needSave; }
    bool IntegrationAvailable() const;
    void SimplifyPresentation();

  signals:
    void Changed();
    void ApplyError(LONG error);
};
