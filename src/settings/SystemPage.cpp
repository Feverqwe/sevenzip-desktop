// Adapted from 7-Zip SystemPage.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source state/change/apply bodies; Qt controls and typed platform services.
#include "settings/SystemPage.h"
#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/SystemPageRes.h"
#include "Common/StringConvert.h"
#include "filemanager/HelpUtils.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#ifdef _WIN32
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <shlobj.h>
#else
#include <ShlObj.h>
#endif
#endif

CSysString CModifiedExtInfo::GetString() const {
    const char *s;
    if (State == kExtState_7Zip)
        s = "7-Zip";
    else if (State == kExtState_Clear)
        s = "";
    else if (Other7Zip)
        s = "[7-Zip]";
    else
        return ProgramKey;
    return CSysString(s);
}

int CSystemPage::AddIcon(const UString &iconPath, int iconIndex, const QIcon &nativeIcon) {
    if (!nativeIcon.isNull()) {
        _imageList.Add(nativeIcon);
        return int(_imageList.Size() - 1);
    }
    if (iconPath.IsEmpty())
        return -1;
    if (iconIndex == -1)
        iconIndex = 0;
#ifdef _WIN32
    // Source AddIcon's environment expansion and indexed Windows icon read.
    UString path;
    const DWORD size = MAX_PATH + 10;
    const DWORD needLen = ::ExpandEnvironmentStringsW(iconPath, path.GetBuf(size + 2), size);
    path.ReleaseBuf_CalcLen(size);
    if (needLen == 0 || needLen >= size)
        path = iconPath;
    HICON hicon;
    const UINT num = ExtractIconExW(path, iconIndex, nullptr, &hicon, 1);
    if (num != 1 || !hicon)
        return -1;
    const QIcon icon(QPixmap::fromImage(QImage::fromHICON(hicon)));
    DestroyIcon(hicon);
#else
    const QIcon icon = QFileIconProvider().icon(QFileInfo(QString::fromWCharArray(iconPath.Ptr())));
#endif
    if (icon.isNull())
        return -1;
    _imageList.Add(icon);
    return int(_imageList.Size() - 1);
}

CSystemPage::CSystemPage(IFolderManager *manager, std::shared_ptr<IQtAssociationServices> services,
                         QWidget *parent)
    : QWidget(parent), _services(std::move(services)) {
    setObjectName("systemPage");
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    _label = new QLabel(this);
    _label->setObjectName("systemAssociateLabel");
    QtBindLangText(_label, IDT_SYSTEM_ASSOCIATE, "Associate 7-Zip with:");
    layout->addWidget(_label);
    auto *buttons = new QGridLayout;
    buttons->setColumnMinimumWidth(0, 80);
    buttons->setColumnStretch(1, 1);
    buttons->setColumnStretch(2, 1);
    _current = new QPushButton("+", this);
    _current->setObjectName("systemCurrent");
    _current->setFocusPolicy(Qt::StrongFocus);
    _all = new QPushButton("+", this);
    _all->setObjectName("systemAll");
    _all->setFocusPolicy(Qt::StrongFocus);
    buttons->addWidget(_current, 0, 1, Qt::AlignHCenter);
    buttons->addWidget(_all, 0, 2, Qt::AlignHCenter);
    layout->addLayout(buttons);
    _listView = new QTreeWidget(this);
    _listView->setObjectName("systemAssociations");
    _listView->setRootIsDecorated(false);
    _listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _listView->setColumnCount(3);
    _listView->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    _listView->header()->resizeSection(0, 80);
    _listView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    _listView->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    _listView->installEventFilter(this);
    layout->addWidget(_listView);
    ReloadLangItems();
    connect(_current, &QPushButton::clicked, this, [this] { ChangeState(0); });
    connect(_all, &QPushButton::clicked, this, [this] { ChangeState(1); });
    connect(_listView, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (QApplication::keyboardModifiers() == Qt::NoModifier && column >= 1 && column <= 2) {
            CUIntVector indices;
            indices.Add(unsigned(_listView->indexOfTopLevelItem(item)));
            ChangeState(unsigned(column - 1), indices);
        }
    });
    // Refresh the Qt choices before external state observers run. The bulk
    // selector already blocks intermediate Changed notifications.
    connect(this, &CSystemPage::Changed, this, &CSystemPage::RefreshFormatChoices);
    OnInit(manager);
}

void CSystemPage::ReloadLangItems() {
    QtReloadLangTexts(*this);
    QString userName = _services ? _services->CurrentUserName() : QString();
    if (userName.isEmpty())
        userName = QCoreApplication::translate("SystemPage", "Current User");
    _listView->setHeaderLabels({QtLangText(IDS_PROP_FILE_TYPE, "File type"), userName,
                                QtLangText(IDS_SYSTEM_ALL_USERS, "All users")});
    _current->setAccessibleName(_listView->headerItem()->text(1));
    _all->setAccessibleName(_listView->headerItem()->text(2));
    RefreshAvailability();
}

bool CSystemPage::OnInit(IFolderManager *manager) {
    _needSave = false;
    _extDB.Exts.Clear();
    _extDB.Read(manager);
    _items.Clear();
    _imageList.Clear();
    _listView->clear();
    FOR_VECTOR(i, _extDB.Exts) {
        const CExtPlugins &extInfo = _extDB.Exts[i];
        CAssoc assoc;
        const CPluginToIcon &plug = extInfo.Plugins[0];
        assoc.SevenZipImageIndex =
            AddIcon(plug.IconPath.IsEmpty() && _services ? _services->OwnIconPath() : plug.IconPath,
                    plug.IconIndex, _services ? _services->OwnIcon() : QIcon());
        auto *item = new QTreeWidgetItem(_listView);
        item->setText(0, QString::fromWCharArray(extInfo.Ext.Ptr()));
        for (unsigned g = 0; g < 2; g++) {
            CModifiedExtInfo &mi = assoc.Pair[g];
            if (_services)
                _services->Read(g, extInfo.Ext, mi);
            PrepareAssociationPresentation(g, i, mi);
            item->setText(int(g + 1),
                          QString::fromWCharArray(GetUnicodeString(mi.GetString()).Ptr()));
        }
        _items.Add(assoc);
        const int imageIndex = assoc.GetIconIndex();
        if (imageIndex != -1)
            item->setIcon(0, _imageList[(unsigned)imageIndex]);
    }
    RefreshAvailability();
    if (!_items.IsEmpty())
        _listView->setCurrentItem(_listView->topLevelItem(0), 0, QItemSelectionModel::NoUpdate);
    return true;
}

void CSystemPage::RefreshAvailability() {
    for (unsigned g = 0; g < 2; g++) {
        auto *button = g == 0 ? _current : _all;
        button->setVisible(_services && _services->CanApply(g));
        bool available = false;
        FOR_VECTOR(i, _extDB.Exts) {
            const bool supported = _services && _services->CanApplyFormat(g, _extDB.Exts[i].Ext);
            available = available || supported;
            _listView->topLevelItem(int(i))->setToolTip(
                int(g + 1),
                supported ? QString()
                : _services && !_services->CanApply(g)
                    ? _services->UnavailableReason(g)
                    : QCoreApplication::translate(
                          "SystemPage", "Association changes are unavailable for this format."));
        }
        button->setEnabled(available);
        button->setToolTip(
            available ? _services->ApplyExplanation(g)
            : _services && !_services->CanApply(g)
                ? _services->UnavailableReason(g)
                : QCoreApplication::translate(
                      "SystemPage", "No supported formats are available for association changes."));
    }
    RefreshFormatChoices();
}

UString CSystemPage::OwnFormatIconPath(unsigned realIndex) const {
#ifdef _WIN32
    const UString &path = _extDB.Exts[realIndex].Plugins[0].IconPath;
    if (!path.IsEmpty())
        return path;
#else
    (void)realIndex;
#endif
    // Native desktop/bundle registrations use the application's native
    // identity rather than a Windows DLL DefaultIcon resource path.
    return _services ? _services->OwnIconPath() : UString();
}

void CSystemPage::PrepareAssociationPresentation(unsigned group, unsigned realIndex,
                                                 CModifiedExtInfo &info) {
    info.SetState(OwnFormatIconPath(realIndex));
    info.ImageIndex = AddIcon(
        info.IconPath, info.IconIndex,
        _services ? _services->AssociationIcon(group, _extDB.Exts[realIndex].Ext) : QIcon());
}

void CSystemPage::RefreshListItem(unsigned group, unsigned listIndex) {
    const CAssoc &assoc = _items[GetRealIndex(listIndex)];
    _listView->topLevelItem(int(listIndex))
        ->setText(int(group + 1),
                  QString::fromWCharArray(GetUnicodeString(assoc.Pair[group].GetString()).Ptr()));
    const int imageIndex = assoc.GetIconIndex();
    _listView->topLevelItem(int(listIndex))
        ->setIcon(0, imageIndex == -1 ? QIcon() : _imageList[(unsigned)imageIndex]);
}

void CSystemPage::ChangeState(unsigned group, const CUIntVector &requested) {
    if (!_services || !_services->CanApply(group))
        return;
    // Native MIME/UTType services cannot register an unknown platform type.
    // Filter unavailable rows before the original mixed-selection algorithm.
    CUIntVector indices;
    FOR_VECTOR(index, requested)
    if (_services->CanApplyFormat(group, _extDB.Exts[GetRealIndex(requested[index])].Ext))
        indices.Add(requested[index]);
    if (indices.IsEmpty())
        return;
    bool thereAreClearItems = false;
    unsigned counters[3] = {0, 0, 0};
    unsigned i;
    for (i = 0; i < indices.Size(); i++) {
        const CModifiedExtInfo &mi = _items[GetRealIndex(indices[i])].Pair[group];
        int state = kExtState_7Zip;
        if (mi.State == kExtState_7Zip)
            state = kExtState_Clear;
        else if (mi.State == kExtState_Clear) {
            thereAreClearItems = true;
            if (mi.Other)
                state = kExtState_Other;
        }
        counters[state]++;
    }
    int state = kExtState_Clear;
    if (counters[kExtState_Other] != 0)
        state = kExtState_Other;
    else if (counters[kExtState_7Zip] != 0)
        state = kExtState_7Zip;
    for (i = 0; i < indices.Size(); i++) {
        unsigned listIndex = indices[i];
        CAssoc &assoc = _items[GetRealIndex(listIndex)];
        CModifiedExtInfo &mi = assoc.Pair[group];
        bool change = false;
        switch (state) {
        case kExtState_Clear:
            change = true;
            break;
        case kExtState_Other:
            change = mi.Other;
            break;
        default:
            change = !(mi.Other && thereAreClearItems);
            break;
        }
        if (change) {
            mi.State = state;
            RefreshListItem(group, listIndex);
        }
    }
    _needSave = true;
    emit Changed();
}

void CSystemPage::ChangeState(unsigned group) {
    CUIntVector indices;
    for (auto *item : _listView->selectedItems())
        indices.Add(unsigned(_listView->indexOfTopLevelItem(item)));
    if (indices.IsEmpty())
        FOR_VECTOR(i, _items)
    indices.Add(i);
    ChangeState(group, indices);
}

static UString GetProgramCommand() {
    UString s('"');
    s += QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString().c_str();
    s += "\" \"%1\"";
    return s;
}

LONG CSystemPage::OnApply() {
    if (!_needSave)
        return 0;
    const UString command = GetProgramCommand();
    LONG res = 0;
    FOR_VECTOR(listIndex, _extDB.Exts) {
        unsigned realIndex = GetRealIndex(listIndex);
        const CExtPlugins &extInfo = _extDB.Exts[realIndex];
        CAssoc &assoc = _items[realIndex];
        for (unsigned g = 0; g < 2; g++) {
            CModifiedExtInfo &mi = assoc.Pair[g];
            if (mi.OldState != mi.State) {
                LONG res2 = 0;
                if (mi.State == kExtState_7Zip) {
                    UString title(QCoreApplication::translate("SystemPage", "%1 Archive")
                                      .arg(QString::fromWCharArray(extInfo.Ext))
                                      .toStdWString()
                                      .c_str());
                    const CPluginToIcon &plug = extInfo.Plugins[0];
                    res2 = _services->Add(g, extInfo.Ext, mi, title, command, plug.IconPath,
                                          plug.IconIndex);
                } else if (mi.State == kExtState_Clear)
                    res2 = _services->Delete(g, extInfo.Ext, mi);
                if (res == 0)
                    res = res2;
                if (res2 == 0)
                    mi.OldState = mi.State;
                mi.State = mi.OldState;
                if (res2 == 1237 && _services->Read(g, extInfo.Ext, mi))
                    PrepareAssociationPresentation(g, realIndex, mi);
                RefreshListItem(g, listIndex);
            }
        }
    }
    _services->NotifyChanged();
    WasChanged = true;
    _needSave = false;
    // Native MIME/UTType changes affect aliases together. After the source
    // Apply loop finishes (and no pending decisions remain), show their actual
    // native handlers. Windows keeps independent source extension semantics.
    if (_services->RefreshNativeStateAfterApply())
        FOR_VECTOR(listIndex, _extDB.Exts) {
            const unsigned realIndex = GetRealIndex(listIndex);
            for (unsigned g = 0; g < 2; ++g) {
                CModifiedExtInfo detected;
                if (_services->Read(g, _extDB.Exts[realIndex].Ext, detected)) {
                    PrepareAssociationPresentation(g, realIndex, detected);
                    _items[realIndex].Pair[g] = detected;
                    RefreshListItem(g, listIndex);
                }
            }
        }
    RefreshAvailability();
    if (res != 0)
        emit ApplyError(res);
    return res;
}

bool CSystemPage::eventFilter(QObject *object, QEvent *event) {
    if (object == _listView && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool ctrl = key->modifiers().testFlag(Qt::ControlModifier);
        if (!key->modifiers().testFlag(Qt::AltModifier)) {
            if ((ctrl && key->key() == Qt::Key_A) || (!ctrl && key->key() == Qt::Key_Asterisk)) {
                _listView->selectAll();
                return true;
            }
            if (!ctrl) {
                switch (key->key()) {
                case Qt::Key_Return:
                case Qt::Key_Enter:
                case Qt::Key_Space:
                    ChangeState(0);
                    return true;
                case Qt::Key_Plus:
                case Qt::Key_Minus:
                case Qt::Key_Comma:
                case Qt::Key_Slash:
                    ChangeState(1);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

void CSystemPage::OnNotifyHelp() { ShowHelpWindow("FM/options.htm#system", this); }
