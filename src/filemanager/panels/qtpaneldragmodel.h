// Declarations adapted from 7-Zip PanelDrag.cpp (0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include "common/collections/MyVector.h"
#include <memory>
#include <optional>
#ifdef _WIN32
#include <oleidl.h>
#endif
class CApp;
class CPanel;
struct CSelectedState;
class QtOwnedTempLease;
constexpr UInt32 k_SourceFlags_DoNotProcessInTarget = 1 << 1,
                 k_SourceFlags_DoNotWaitFinish = 1 << 3, k_SourceFlags_WaitFinish = 1 << 4,
                 k_SourceFlags_TempFiles = 1 << 6, k_SourceFlags_NamesAreParent = 1 << 7,
                 k_SourceFlags_SetTargetFolder = 1 << 8,
                 k_SourceFlags_SetTargetFolder_NonEmpty = 1 << 9,
                 k_SourceFlags_NeedExtractOpToFs = 1 << 10, k_SourceFlags_Copy_WasCalled = 1 << 11,
                 k_SourceFlags_LeftButton = 1 << 14, k_SourceFlags_RightButton = 1 << 15;
constexpr UInt32 k_TargetFlags_WasCanceled = 1 << 0, k_TargetFlags_MustBeProcessedBySource = 1 << 1,
                 k_TargetFlags_WasProcessed = 1 << 2, k_TargetFlags_DoNotWaitFinish = 1 << 3,
                 k_TargetFlags_WaitFinish = 1 << 4, k_TargetFlags_MenuWasShown = 1 << 16;
enum Enum_DragTargetMode {
    k_DragTargetMode_None = 0,
    k_DragTargetMode_Leave = 1,
    k_DragTargetMode_Enter = 2,
    k_DragTargetMode_Over = 3,
    k_DragTargetMode_Drop_Begin = 4,
    k_DragTargetMode_Drop_End = 5
};
enum Enum_FolderType {
    k_FolderType_None,
    k_FolderType_Unknown = 1,
    k_FolderType_Fs = 2,
    k_FolderType_AltStreams = 3,
    k_FolderType_Archive = 4
};
namespace NDragMenu {
enum Enum_CmdId { k_None = 0, k_Cancel = 1, k_Copy_Base = 2, k_Copy_ToArc = 3, k_AddToArc = 4 };
constexpr UInt32 k_MenuFlag_Copy = 1 << 14, k_MenuFlag_Move = 1 << 15,
                 k_MenuFlags_CmdMask = (1 << 7) - 1;
} // namespace NDragMenu
struct CTargetTransferInfo {
    UInt32 Flags = 0;
    UInt32 FuncType = 0;
    UInt32 Cmd_Effect = 0;
    UInt32 KeyState = 0, OkEffects = 0, Cmd_Type = 0, FolderType = 0;
    int X = 0, Y = 0;
};
struct CDataObject_SetTransfer {
    CTargetTransferInfo Target;
};
#ifndef _WIN32
constexpr DWORD MK_LBUTTON = 1, MK_RBUTTON = 2, MK_SHIFT = 4, MK_CONTROL = 8, MK_MBUTTON = 16,
                MK_ALT = 32;
constexpr DWORD DROPEFFECT_NONE = 0, DROPEFFECT_COPY = 1, DROPEFFECT_MOVE = 2, DROPEFFECT_LINK = 4;
constexpr HRESULT DRAGDROP_S_DROP = 0x00040100, DRAGDROP_S_CANCEL = 0x00040101,
                  DRAGDROP_S_USEDEFAULTCURSORS = 0x00040102;
#endif
class IQtNativeDragData {
  public:
    virtual ~IQtNativeDragData() = default;
    virtual HRESULT GetNames(UStringVector &) = 0;
    virtual bool SetTargetFolder(const UString &) = 0;
    virtual bool SetTransfer(const CTargetTransferInfo &) = 0;
    virtual bool ReadTransferFlags(UInt32 &) = 0;
    virtual void SendPerformedEffect(DWORD) = 0;
};
class CDataObject : public std::enable_shared_from_this<CDataObject> {
  public:
    CPanel *Panel = nullptr;
    CRecordVector<UInt32> Indices;
    UStringVector Messages, Names;
    UString SrcDirPrefix_Temp, DestDirPrefix_FromTarget;
    bool IsRightButton = false, IsTempFiles = false, UsePreGlobal = false;
    bool DoNotProcessInTarget = false, NeedCall_Copy = false, Copy_WasCalled = false;
    HRESULT Copy_HRESULT = S_OK;
    bool m_Transfer_WasSet = false;
    CDataObject_SetTransfer m_Transfer;
    std::shared_ptr<QtOwnedTempLease> OwnedTempLease;
    std::shared_ptr<IQtNativeDragData> NativeData;
    bool ExternalReceiverUnconfirmed = false;
    bool m_DestDirPrefix_FromTarget_WasSet = false;
    void SetTransfer(const CTargetTransferInfo &target);
    UInt32 GetTransferFlags() const;
    bool ReadTransferFlags(UInt32 &flags) const;
    HRESULT GetData(UStringVector &names);
    void CopyFromPanelTo_Folder();
    bool BeginExternalReceiver();
    void FinishExternalReceiver();
};
class CDropSource {
    DWORD m_Effect = DROPEFFECT_NONE;

  public:
    CDataObject *DataObjectSpec = nullptr;
    HRESULT DragProcessing_HRESULT = S_OK;
    bool DragProcessing_WasFinished = false;
    HRESULT QueryContinueDrag(BOOL escapePressed, DWORD keyState);
    HRESULT GiveFeedback(DWORD effect);
};
class IQtPanelDragServices {
  public:
    virtual ~IQtPanelDragServices() = default;
    virtual HRESULT DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                               DWORD &effect) = 0;
    virtual HRESULT CompressFiles(CPanel &, const UString &, const UString &, const UStringVector &,
                                  bool) {
        return E_NOTIMPL;
    }
};
// Win32 hit testing and storage identity are narrow presentation/platform services.
class IQtDropTargetPresentation {
  public:
    virtual ~IQtDropTargetPresentation() = default;
    virtual int PanelAtPoint(int x, int y) const = 0;
    virtual bool PanelEnabled(unsigned panel) const = 0;
    virtual int FolderAtPoint(unsigned panel, int x, int y, UString &name) const = 0;
    virtual void Highlight(unsigned panel, int item, bool enabled) = 0;
    virtual bool IsSameDrive(const UString &destination, const UStringVector &sources) const = 0;
    virtual bool ConfirmCopyToArchive(CPanel &, DWORD) { return false; }
    virtual UInt32 Drag_OnContextMenu(int, int, UInt32) { return NDragMenu::k_Cancel; }
};
class CDropTarget {
    bool m_DropIsAllowed = false, m_PanelDropIsAllowed = false;
    UStringVector m_SourcePaths;
    CPanel *m_Panel = nullptr;
    bool m_IsAppTarget = false, m_TargetPath_WasSent_ToDataObject = false;
    bool m_TargetPath_NonEmpty_WasSent_ToDataObject = false;
    bool m_IsRightButton = false;
    CDataObject *m_DataObject = nullptr;
    IQtDropTargetPresentation *presentation_ = nullptr;
    void RemoveSelection();
    bool SendToSource_TargetPath_enable(CDataObject *dataObject, bool enablePath);
    void SendToSource_auto(CDataObject *dataObject, const CTargetTransferInfo &info);
    bool SendToSource_TransferInfo(CDataObject *dataObject, const CTargetTransferInfo &info);
    HRESULT LoadNames_From_DataObject(CDataObject *dataObject);
    UInt32 GetFolderType() const;

  public:
    CApp *App = nullptr;
    int SrcPanelIndex = -1, TargetPanelIndex = -1;
    void AttachPresentation(IQtDropTargetPresentation *presentation) {
        presentation_ = presentation;
    }
    void LoadNames_From_DataObject(const UStringVector &paths, bool is7zip);
    void PositionCursor(int x, int y);
    bool IsFsFolderPath() const;
    bool IsItSameDrive() const;
    DWORD GetEffect(DWORD keyState, DWORD allowedEffect) const;
    UString GetTargetPath() const;
    void ClearState();
    HRESULT DragEnter(CDataObject *dataObject, DWORD keyState, int x, int y, DWORD *effect);
    HRESULT DragOver(DWORD keyState, int x, int y, DWORD *effect);
    HRESULT DragLeave();
    HRESULT Drop(CDataObject *dataObject, DWORD keyState, int x, int y, DWORD *effect,
                 std::optional<CSelectedState> *refreshSelection = nullptr);
    CPanel *TargetPanel() const { return m_Panel; }
    bool IsAppTarget() const { return m_IsAppTarget; }
    bool IsMenuDrop() const { return m_IsRightButton; }
};
