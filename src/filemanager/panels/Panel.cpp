// Adapted from 7-Zip CPP/7zip/UI/FileManager/Panel.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/panels/Panel.h"
#include "7zip/PropID.h"
#include "7zip/UI/Common/ArchiveName.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "Windows/FileName.h"
#include "Windows/PropVariant.h"
#include "Windows/WinDefs.h"
#include "archive/agent/IFolderArchive.h"
#include "archive/operations/ZipRegistry.h"
#include "dialogs/compress/qtcompresscallservices.h"
#include "filemanager/App.h"
#include "filemanager/external/qtexternalfilesession.h"
#include "filemanager/external/qtexternalsessionowner.h"
#include "filemanager/panels/qtpanelkey.h"
#include "localization/LangUtils.h"
#include "operations/CompressCall.h"
#include "ui/progress/ProgressDialog2.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

HRESULT CPanel::Create(QWidget *mainWindow, QWidget *parentWindow, UINT id,
                       const UString &currentFolderPrefix, const UString &arcFormat,
                       CPanelCallback *panelCallback, CAppState *appState, bool needOpenArc,
                       COpenResult &openRes) {
    _mainWindow = mainWindow;
    _parentWindow = parentWindow;
    _processTimer = true;
    _processNotify = true;
    _processStatusBar = true;
    _panelCallback = panelCallback;
    _appState = appState;
    _baseID = id;
    _comboBoxID = _baseID + 3;
    _statusBarID = _comboBoxID + 1;
    UString cfp = currentFolderPrefix;
    if (!currentFolderPrefix.IsEmpty())
        if (currentFolderPrefix[0] == L'.') {
            FString cfpF;
            if (NWindows::NFile::NDir::MyGetFullPathName(us2fs(currentFolderPrefix), cfpF))
                cfp = fs2us(cfpF);
        }
    RINOK(BindToPath(cfp, arcFormat, openRes))
    if (needOpenArc && !openRes.ArchiveIsOpened)
        return S_OK;
    // Qt service replaces CreateEx and its WM_CREATE dispatch.
    if (!_createPresentation)
        return E_NOTIMPL;
    if (!OnCreate())
        return E_FAIL;
    PanelCreated = true;
    return S_OK;
}

bool CPanel::OnCreate() {
    _sortID = kpidName;
    _ascending = true;
    _lastFocusedIsList = true;
    _comboBoxEdit._panel = this;
    if (_listViewMode != 0)
        _listViewMode = 3;
    // Original list/header/up/status child controls are Qt presentation. The
    // source owner retains mode, sort, focus, binding and processing state.
    if (!_createPresentation(*this))
        return false;
    RefreshListCtrl();
    return true;
}

CPanel::CPanel() = default;

UString CPanel::GetResumeFolderPath() const {
    return _parentFolders.IsEmpty() ? _currentFolderPrefix : _parentFolders[0].ParentFolderPath;
}

void CPanel::ExitExternalWorkers(bool hardExit) {
    if (_exitEventLauncher)
        _exitEventLauncher->Exit(hardExit);
    for (const auto &weak : _externalSessions)
        if (const auto session = weak.lock())
            session->Stop();
}

bool CPanel::HasExternalWorkers() const {
    if (_exitEventLauncher && _exitEventLauncher->PendingSessions.load() != 0)
        return true;
    for (const auto &weak : _externalSessions)
        if (const auto session = weak.lock(); session && session->Launcher().PendingSessions.load())
            return true;
    return false;
}

void CPanel::RebindWindowHost(QWidget *window, CPanelCallback *callback, CAppState *state) {
    _mainWindow = window;
    _parentWindow = window;
    _panelCallback = callback;
    _appState = state;
    _createPresentation = {};
}

void CPanel::MessageBox_Error_LangID(UINT resourceID) const {
    MessageBox_Error(LangString(resourceID));
}

void CPanel::MessageBox_Error_Caption(LPCWSTR message, LPCWSTR caption) const {
    if (!_archiveOpenDialogs)
        return;
    QMessageBox box(QMessageBox::Critical, QString::fromWCharArray(caption),
                    QString::fromWCharArray(message), QMessageBox::Ok, GetParent());
    box.setObjectName("panelArchiveError");
    box.setTextFormat(Qt::PlainText);
#ifdef Q_OS_MACOS
    // Native macOS message boxes omit captions. Retain an operation-specific
    // source caption as the heading, as in the existing Comment error adapter.
    if (QString::fromWCharArray(caption) != QStringLiteral("7-Zip")) {
        box.setInformativeText(box.text());
        box.setText(QString::fromWCharArray(caption));
    }
#endif
    box.exec();
}

void CPanel::MessageBox_Error(const UString &message) const {
    MessageBox_Error_Caption(message, L"7-Zip");
}

static UString ErrorHResult_To_Message(HRESULT errorCode) {
    if (errorCode == 0)
        errorCode = E_FAIL;
    return HResultToMessage(errorCode);
}

void CPanel::MessageBox_Error_HRESULT(HRESULT errorCode) const {
    MessageBox_Error(ErrorHResult_To_Message(errorCode));
}

void CPanel::MessageBox_Error_HRESULT_Caption(HRESULT errorCode, LPCWSTR caption) const {
    MessageBox_Error_Caption(ErrorHResult_To_Message(errorCode), caption);
}

void CPanel::MessageBox_Error_2Lines_Message_HRESULT(LPCWSTR message, HRESULT errorCode) const {
    UString m = message;
    m.Add_LF();
    m += ErrorHResult_To_Message(errorCode);
    MessageBox_Error(m);
}

void CPanel::ReleasePanel() {
    Disable_Processing_Timer_Notify_StatusBar();
    // Local per-panel launcher replaces the original global exit owner. Join
    // before folder cleanup, since its workers may still refer to this panel.
    ExitExternalWorkers(true);
    // It's for unloading COM dll's: don't change it.
    CloseOpenFolders();
    // Win32 context-menu COM objects have no Qt owner here. Their mandatory
    // menu integration remains P0-03/P0-09/P2-09; do not add dummy COM members.
}

CPanel::~CPanel() {
    // Native destructor folder cleanup, after stopping Qt notifications and
    // joining the per-panel external-file workers that can refer to this owner.
    Disable_Processing_Timer_Notify_StatusBar();
    ExitExternalWorkers(true);
    CloseOpenFolders();
}

int CPanel::FindDir_InOperatedList(const CRecordVector<UInt32> &operatedIndices) const {
    const UInt32 *indices = operatedIndices.ConstData();
    const unsigned numItems = operatedIndices.Size();
    for (unsigned i = 0; i < numItems; i++)
        // The source load stage owns the cache; IsItem_Folder also supports
        // provider queries before rows have been loaded.
        if (IsItem_Folder(indices[i]))
            return (int)i;
    return -1;
}

UString CPanel::GetFolderTypeID() const {
    {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetFolderProperty(kpidType, &prop) == S_OK)
            if (prop.vt == VT_BSTR)
                return (const wchar_t *)prop.bstrVal;
    }
    return UString();
}

bool CPanel::IsFolderTypeEqTo(const char *s) const {
    return StringsAreEqual_Ascii(GetFolderTypeID(), s);
}

bool CPanel::IsRootFolder() const { return IsFolderTypeEqTo("RootFolder"); }
bool CPanel::IsFSFolder() const { return IsFolderTypeEqTo("FSFolder"); }
bool CPanel::IsFSDrivesFolder() const { return IsFolderTypeEqTo("FSDrives"); }
bool CPanel::IsNetFolder() const { return IsFolderTypeEqTo("NetFolder"); }
bool CPanel::IsAltStreamsFolder() const { return IsFolderTypeEqTo("AltStreamsFolder"); }
bool CPanel::IsArcFolder() const { return GetFolderTypeID().IsPrefixedBy_Ascii_NoCase("7-Zip"); }

bool CPanel::IsHashFolder() const {
    if (_folder) {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetFolderProperty(kpidIsHash, &prop) == S_OK)
            if (prop.vt == VT_BOOL)
                return VARIANT_BOOLToBool(prop.boolVal);
    }
    return false;
}

UString CPanel::GetFsPath() const {
    if (IsFSDrivesFolder() && !IsDeviceDrivesPrefix() && !IsSuperDrivesPrefix())
        return UString();
    return _currentFolderPrefix;
}

UString CPanel::GetDriveOrNetworkPrefix() const {
    if (!IsFSFolder())
        return UString();
    UString drive = GetFsPath();
    drive.DeleteFrom(NWindows::NFile::NName::GetRootPrefixSize(drive));
    return drive;
}

void CPanel::SetListViewMode(UInt32 index) {
    _listViewMode = index == 0 ? 0 : 3;
    // Qt style/stack setters replace GetStyle/SetStyle on the native list.
    if (_viewModePresentation)
        _viewModePresentation(_listViewMode);
}

void CPanel::ChangeFlatMode() {
    _flatMode = !_flatMode;
    if (!_parentFolders.IsEmpty())
        _flatModeForArc = _flatMode;
    else
        _flatModeForDisk = _flatMode;
    RefreshListCtrl_SaveFocused();
}

// Original AddToArchive body. The retained engine/Qt modal replaces the
// process-global CompressFiles entry; only the root prefix is platform-specific.
void CPanel::AddToArchive(IQtCompressCallServices &services) {
    if (!Is_IO_FS_Folder()) {
        MessageBox_Error_UnsupportOperation();
        return;
    }
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    if (indices.Size() == 0) {
        MessageBox_Error_LangID(IDS_SELECT_FILES);
        return;
    }
    UString destCurDirPrefix = GetFsPath();
    if (IsFSDrivesFolder()) {
        destCurDirPrefix = ROOT_FS_FOLDER;
    }
    UStringVector names;
    GetFilePaths(indices, names);
    UString baseName;
    const UString arcName = CreateArchiveName(names,
                                              false,   // isHash
                                              nullptr, // CFileInfo *fi
                                              baseName);
    const HRESULT res = services.CompressSelection(destCurDirPrefix, arcName, L"",
                                                   true, // addExtension
                                                   names,
                                                   false,  // email
                                                   true,   // showDialog
                                                   false); // waitFinish
    if (res != S_OK) {
        if (destCurDirPrefix.Len() >= MAX_PATH)
            MessageBox_Error_LangID(IDS_MESSAGE_UNSUPPORTED_OPERATION_FOR_LONG_PATH_FOLDER);
    }
    // KillSelection();
}

void CPanel::GetFilePaths(const CRecordVector<UInt32> &operatedIndices,
                          UStringVector &paths) const {
    paths.ClearAndReserve(operatedIndices.Size());
    UString path = GetFsPath();
    const unsigned prefixLen = path.Len();
    const UInt32 *indices = operatedIndices.ConstData();
    const unsigned numItems = operatedIndices.Size();
    for (unsigned i = 0; i < numItems; i++) {
        path.DeleteFrom(prefixLen);
        Add_ItemRelPath2_To_String(indices[i], path);
        paths.AddInReserved(path);
    }
}

void CPanel::SetFocusToList() {
    if (auto *combo = qobject_cast<QComboBox *>(_headerComboBox.data()))
        combo->hidePopup();
    if (_listView)
        _listView->setFocus();
}

void CPanel::SetFocusToPath() {
    if (_addressFocusPresentation) {
        _addressFocusPresentation();
    } else if (_headerComboBox) {
        _headerComboBox->setFocus();
    }
    if (auto *combo = qobject_cast<QComboBox *>(_headerComboBox.data()))
        combo->showPopup();
}

void CPanel::SetFocusToLastRememberedItem() {
    if (_lastFocusedIsList)
        SetFocusToList();
    else if (_addressFocusPresentation)
        _addressFocusPresentation();
    else if (_headerComboBox)
        _headerComboBox->setFocus();
}

// Presentation binding replaces the two Win32 control handles. Switching the
// displayed list must not reset the last focused control (OnCreate defaults).
void CPanel::AttachPresentation(QWidget *list, QWidget *address) {
    if (_listView)
        _listView->removeEventFilter(this);
    if (_headerComboBox)
        _headerComboBox->removeEventFilter(this);
    _listView = list;
    _selectionList.SetPresentationView(qobject_cast<QAbstractItemView *>(list));
    _headerComboBox = address;
    if (_listView)
        _listView->installEventFilter(this);
    if (_headerComboBox)
        _headerComboBox->installEventFilter(this);
}

// function from ContextMenu.cpp
UString GetSubFolderNameForExtract(const UString &arcPath);

static UString GetSubFolderNameForExtract2(const UString &arcPath) {
    int slashPos = arcPath.ReverseFind_PathSepar();
    UString s;
    UString name = arcPath;
    if (slashPos >= 0) {
        s = arcPath.Left((unsigned)(slashPos + 1));
        name = arcPath.Ptr((unsigned)(slashPos + 1));
    }
    s += GetSubFolderNameForExtract(name);
    return s;
}

void CPanel::ExtractArchives(IQtCompressCallServices &services) {
    if (!_parentFolders.IsEmpty()) {
        _panelCallback->OnCopy(false, false);
        return;
    }
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Operated(indices);
    if (indices.Size() != 1 || IsItem_Folder(indices[0]))
        return;
    UStringVector paths;
    GetFilePaths(indices, paths);
    UString outFolder = GetFsPath();
    outFolder += GetSubFolderNameForExtract2(GetItemRelPath(indices[0]));
    outFolder.Add_PathSepar();
    CContextMenuInfo ci;
    ci.Load(_settingsPath);
    services.ExtractArchives(paths, outFolder, true, false, ci.WriteZone);
}

void CPanel::TestArchives(IQtCompressCallServices *services) {
    CRecordVector<UInt32> indices;
    Get_ItemIndices_OperSmart(indices);
    CMyComPtr<IArchiveFolder> archiveFolder;
    _folder.QueryInterface(IID_IArchiveFolder, &archiveFolder);
    if (archiveFolder) {
        CCopyToOptions options;
        options.streamMode = true;
        options.showErrorMessages = true;
        options.testMode = true;
        UInt32 folderItems = 0;
        const char *scope = QT_TRANSLATE_NOOP("ArchiveTest", "Scope: selected items");
        if (_folder->GetNumberOfItems(&folderItems) == S_OK && indices.Size() == folderItems) {
            scope = GetFolderPath(_folder).IsEmpty()
                        ? QT_TRANSLATE_NOOP("ArchiveTest", "Scope: entire archive")
                        : QT_TRANSLATE_NOOP("ArchiveTest", "Scope: current folder");
        }
        options.TestScope = ToUString(QCoreApplication::translate("ArchiveTest", scope));

        UStringVector messages;
        HRESULT res = CopyTo(options, indices, &messages);
        if (res != S_OK) {
            if (res != E_ABORT)
                MessageBox_Error_HRESULT(res);
        }
        return;
    }
    if (!IsFSFolder()) {
        MessageBox_Error_UnsupportOperation();
        return;
    }
    UStringVector paths;
    GetFilePaths(indices, paths);
    if (paths.IsEmpty()) {
        MessageBox_Error_LangID(IDS_SELECT_FILES);
        return;
    }
    if (services)
        services->TestArchives(paths, false);
    else
        MessageBox_Error_HRESULT(E_NOTIMPL);
}

bool CPanel::eventFilter(QObject *watched, QEvent *event) {
    // CMyListView WM_SETFOCUS and OnNotifyComboBox CBEN_BEGINEDIT.
    if (event->type() == QEvent::FocusIn) {
        if (watched == _listView)
            _lastFocusedIsList = true;
        else if (watched == _headerComboBox)
            _lastFocusedIsList = false;
        if (_panelCallback && _processNotify)
            _panelCallback->PanelWasFocused();
    }
    return QObject::eventFilter(watched, event);
}

bool CPanel::OnListViewKeyDown(const QtPanelKeyDown &key, CApp &app) {
    // CMyListView::OnMessage handles WM_KEYDOWN before LVN_KEYDOWN reaches
    // OnKeyDown. Commands supply the existing Qt open/parent publication seam.
    const bool alt = key.Alt;
    const bool ctrl = key.Control;
    const bool shift = key.Shift;
    switch (key.VKey) {
    case Qt::Key_PageDown:
        if (ctrl && !alt && !shift) {
            app.ExecuteCommand(IDM_OPEN_INSIDE);
            return true;
        }
        break;
    case Qt::Key_PageUp:
        if (ctrl && !alt && !shift) {
            app.ExecuteCommand(IDM_OPEN_PARENT_FOLDER);
            return true;
        }
        break;
    }
    return false;
}

bool CMyComboBoxEdit::OnMessage(const QtPanelKeyDown &key, CApp &g_App) {
    // Qt decodes WM_SYSKEYDOWN/WM_KEYDOWN and GetKeyState. Return/Escape
    // completion and WM_CHAR suppression belong to the address control adapter.
    if (!_panel)
        return false;
    if (key.Alt) {
        switch (key.VKey) {
        case Qt::Key_F1:
        case Qt::Key_F2:
            if (!key.Control && !key.Shift) {
                _panel->_panelCallback->SetFocusToPath(key.VKey == Qt::Key_F1 ? 0 : 1);
                return true;
            }
            break;
        }
        return false;
    }
    switch (key.VKey) {
    case Qt::Key_Tab:
        _panel->SetFocusToList();
        return true;
    case Qt::Key_F9:
        if (!key.Control && !key.Shift) {
            g_App.ExecuteCommand(IDM_VIEW_TWO_PANELS);
            return true;
        }
        break;
    case 'W':
        if (key.Control) {
            QTimer::singleShot(0, _panel, [&g_App] { g_App.ExecuteCommand(8); });
            return true;
        }
        break;
    }
    return false;
}

bool CPanel::OnAddressEditKeyDown(const QtPanelKeyDown &key, CApp &app) {
    return _comboBoxEdit.OnMessage(key, app);
}
