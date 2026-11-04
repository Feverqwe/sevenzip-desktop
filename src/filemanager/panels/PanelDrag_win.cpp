// Windows OLE transport adapted from 7-Zip PanelDrag.cpp.
// Original baseline: b88c6bc09339de32f7d18bbab900686b244f42ba.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
#include "filemanager/panels/Panel.h"
#include <QWidget>

#ifdef _WIN32
#include "7zip/UI/FileManager/EnumFormatEtc.h"
#include "7zip/UI/FileManager/MyCom2.h"
#include "C/7zVersion.h"
#include "Windows/COM.h"
#include "Windows/MemoryGlobal.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "platform/cpu/CpuArch.h"
#include "platform/shell/Shell.h"
#include <cstring>
namespace NPanelDragNative {
using namespace NWindows;
static LPCTSTR const k_Format_7zip_SetTargetFolder = TEXT("7-Zip::SetTargetFolder");
static LPCTSTR const k_Format_7zip_SetTransfer = TEXT("7-Zip::SetTransfer");
static LPCTSTR const k_Format_7zip_GetTransfer = TEXT("7-Zip::GetTransfer");
static const UInt32 k_Struct_Id_SetTranfer = 2; // it's our selected id
static const UInt32 k_Struct_Id_GetTranfer = 3; // it's our selected id

static const UInt64 k_Program_Id = 1; // "7-Zip"

enum E_Program_ISA {
    k_Program_ISA_x86 = 2,
    k_Program_ISA_x64 = 3,
    k_Program_ISA_armt = 4,
    k_Program_ISA_arm64 = 5,
    k_Program_ISA_arm32 = 6,
    k_Program_ISA_ia64 = 9
};

#define k_Program_Ver ((MY_VER_MAJOR << 16) | MY_VER_MINOR)

struct CDataObject_TransferBase {
    UInt32 Struct_Id;
    UInt32 Struct_Size;

    UInt64 Program_Id;
    UInt32 Program_Ver_Main;
    UInt32 Program_Ver_Build;
    UInt32 Program_ISA;
    UInt32 Program_Flags;

    UInt32 ProcessId;
    UInt32 _reserved1[7];

  protected:
    void Init_Program();
};

void CDataObject_TransferBase::Init_Program() {
    Program_Id = k_Program_Id;
    Program_ISA =
#if defined(MY_CPU_AMD64)
        k_Program_ISA_x64
#elif defined(MY_CPU_X86)
        k_Program_ISA_x86
#elif defined(MY_CPU_ARM64)
        k_Program_ISA_arm64
#elif defined(MY_CPU_ARM32)
        k_Program_ISA_arm32
#elif defined(MY_CPU_ARMT) || defined(MY_CPU_ARM)
        k_Program_ISA_armt
#elif defined(MY_CPU_IA64)
        k_Program_ISA_ia64
#else
        0
#endif
        ;
    Program_Flags = sizeof(size_t);
    Program_Ver_Main = k_Program_Ver;
    // Program_Ver_Build = 0;
    ProcessId = GetCurrentProcessId();
}

#if defined(__GNUC__) && !defined(__clang__)
/* 'void* memset(void*, int, size_t)' clearing an object
    of non-trivial type 'struct CDataObject_SetTransfer' */
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

struct CDataObject_GetTransfer : public CDataObject_TransferBase {
    UInt32 Flags;

    UInt32 _reserved2[11];

    CDataObject_GetTransfer() {
        memset(this, 0, sizeof(*this));
        Init_Program();
        Struct_Id = k_Struct_Id_GetTranfer;
        Struct_Size = sizeof(*this);
    }

    bool Check() const {
        return Struct_Size >= sizeof(*this) && Struct_Id == k_Struct_Id_GetTranfer;
    }
};

struct CTargetTransferInfo {
    UInt32 Flags;
    UInt32 FuncType;

    UInt32 KeyState;
    UInt32 OkEffects;
    POINTL Point;

    UInt32 Cmd_Effect;
    UInt32 Cmd_Type;
    UInt32 FolderType;
    UInt32 _reserved3[3];

    CTargetTransferInfo() { memset(this, 0, sizeof(*this)); }
};

struct CDataObject_SetTransfer : public CDataObject_TransferBase {
    CTargetTransferInfo Target;

    void Init() {
        memset(this, 0, sizeof(*this));
        Init_Program();
        Struct_Id = k_Struct_Id_SetTranfer;
        Struct_Size = sizeof(*this);
    }

    bool Check() const {
        return Struct_Size >= sizeof(*this) && Struct_Id == k_Struct_Id_SetTranfer;
    }
};

static ::CTargetTransferInfo ToTyped(const CTargetTransferInfo &s) {
    ::CTargetTransferInfo d;
    d.Flags = s.Flags;
    d.FuncType = s.FuncType;
    d.KeyState = s.KeyState;
    d.OkEffects = s.OkEffects;
    d.X = s.Point.x;
    d.Y = s.Point.y;
    d.Cmd_Effect = s.Cmd_Effect;
    d.Cmd_Type = s.Cmd_Type;
    d.FolderType = s.FolderType;
    return d;
}
static CTargetTransferInfo ToNative(const ::CTargetTransferInfo &s) {
    CTargetTransferInfo d;
    d.Flags = s.Flags;
    d.FuncType = s.FuncType;
    d.KeyState = s.KeyState;
    d.OkEffects = s.OkEffects;
    d.Point.x = s.X;
    d.Point.y = s.Y;
    d.Cmd_Effect = s.Cmd_Effect;
    d.Cmd_Type = s.Cmd_Type;
    d.FolderType = s.FolderType;
    return d;
}
static_assert(sizeof(CDataObject_TransferBase) == 64);
static_assert(sizeof(CDataObject_GetTransfer) == 112);
static_assert(sizeof(CDataObject_SetTransfer) == 112);
static bool CopyNamesToHGlobal(NMemory::CGlobal &hgDrop, const UStringVector &names) {
    size_t totalLen = 1;

    {
        unsigned i;
        for (i = 0; i < names.Size(); i++)
            totalLen += names[i].Len() + 1;

        if (!hgDrop.Alloc(GHND | GMEM_SHARE, totalLen * sizeof(WCHAR) + sizeof(DROPFILES)))
            return false;

        NMemory::CGlobalLock dropLock(hgDrop);
        DROPFILES *dropFiles = (DROPFILES *)dropLock.GetPointer();
        if (!dropFiles)
            return false;
        /* fNC:
            TRUE  : pt specifies the screen coordinates of a point in a window's nonclient area.
            FALSE : pt specifies the client coordinates of a point in the client area.
        */
        dropFiles->fNC = FALSE;
        dropFiles->pt.x = 0;
        dropFiles->pt.y = 0;
        dropFiles->pFiles = sizeof(DROPFILES);
        dropFiles->fWide = TRUE;
        WCHAR *p = (WCHAR *)(void *)((BYTE *)dropFiles + sizeof(DROPFILES));
        for (i = 0; i < names.Size(); i++) {
            const UString &s = names[i];
            const unsigned fullLen = s.Len() + 1;
            MyStringCopy(p, (const WCHAR *)s);
            p += fullLen;
            totalLen -= fullLen;
        }
        *p = 0;
    }
    // if (totalLen != 1) return false;
    return true;
}
class CDataObject Z7_final : public IDataObject, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1_MT(IDataObject)

    Z7_COMWF_B GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM medium) Z7_override;
    Z7_COMWF_B GetDataHere(LPFORMATETC pformatetc, LPSTGMEDIUM medium) Z7_override;
    Z7_COMWF_B QueryGetData(LPFORMATETC pformatetc) Z7_override;

    Z7_COMWF_B GetCanonicalFormatEtc(LPFORMATETC /* pformatetc */,
                                     LPFORMATETC pformatetcOut) Z7_override {
        if (!pformatetcOut)
            return E_INVALIDARG;
        pformatetcOut->ptd = NULL;
        return E_NOTIMPL;
    }

    Z7_COMWF_B SetData(LPFORMATETC etc, STGMEDIUM *medium, BOOL release) Z7_override;
    Z7_COMWF_B EnumFormatEtc(DWORD drection, LPENUMFORMATETC *enumFormatEtc) Z7_override;

    Z7_COMWF_B DAdvise(FORMATETC * /* etc */, DWORD /* advf */, LPADVISESINK /* pAdvSink */,
                       DWORD * /* pdwConnection */) Z7_override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    Z7_COMWF_B DUnadvise(DWORD /* dwConnection */) Z7_override { return OLE_E_ADVISENOTSUPPORTED; }
    Z7_COMWF_B EnumDAdvise(LPENUMSTATDATA *ppenumAdvise) Z7_override {
        if (ppenumAdvise)
            *ppenumAdvise = NULL;
        return OLE_E_ADVISENOTSUPPORTED;
    }

    bool m_PerformedDropEffect_WasSet;
    bool m_LogicalPerformedDropEffect_WasSet;

  private:
    // GetData formats (source to target):
    FORMATETC m_Etc;
    // UINT m_Format_FileOpFlags;
    // UINT m_Format_PreferredDropEffect;

    // SetData() formats (target to source):
    // 7-Zip's format:
    UINT m_Format_7zip_SetTargetFolder;
    UINT m_Format_7zip_SetTransfer;
    UINT m_Format_7zip_GetTransfer; // for GetData()

    UINT m_Format_PerformedDropEffect;
    UINT m_Format_LogicalPerformedDropEffect;
    UINT m_Format_DisableDragText;
    UINT m_Format_IsShowingLayered;
    UINT m_Format_IsShowingText;
    UINT m_Format_DropDescription;
    UINT m_Format_TargetCLSID;

    DWORD m_PerformedDropEffect;
    DWORD m_LogicalPerformedDropEffect;

    NMemory::CGlobal m_hGlobal_HDROP_Pre, m_hGlobal_HDROP_Final;
    std::shared_ptr<::CDataObject> model_;
    HRESULT SetData2(const FORMATETC *, const STGMEDIUM *);

  public:
    explicit CDataObject(std::shared_ptr<::CDataObject> model);
    HRESULT InitNames();
};
HRESULT CDataObject::InitNames() {
    if (!CopyNamesToHGlobal(m_hGlobal_HDROP_Final, model_->Names))
        return E_OUTOFMEMORY;
    if (model_->IsTempFiles) {
        UStringVector names;
        names.Add(model_->SrcDirPrefix_Temp);
        if (!CopyNamesToHGlobal(m_hGlobal_HDROP_Pre, names))
            return E_OUTOFMEMORY;
    }
    return S_OK;
}
CDataObject::CDataObject(std::shared_ptr<::CDataObject> model) : model_(std::move(model)) {
    // GetData formats (source to target):
    // and we use CF_HDROP format to transfer file paths from source to target:
    m_Etc.cfFormat = CF_HDROP;
    m_Etc.ptd = NULL;
    m_Etc.dwAspect = DVASPECT_CONTENT;
    m_Etc.lindex = -1;
    m_Etc.tymed = TYMED_HGLOBAL;

    // m_Format_FileOpFlags          = RegisterClipboardFormat(TEXT("FileOpFlags"));
    // m_Format_PreferredDropEffect  = RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT); //
    // "Preferred DropEffect"

    // SetData() formats (target to source):
    m_Format_7zip_SetTargetFolder = RegisterClipboardFormat(k_Format_7zip_SetTargetFolder);
    m_Format_7zip_SetTransfer = RegisterClipboardFormat(k_Format_7zip_SetTransfer);
    m_Format_7zip_GetTransfer = RegisterClipboardFormat(k_Format_7zip_GetTransfer);

    m_Format_PerformedDropEffect =
        RegisterClipboardFormat(CFSTR_PERFORMEDDROPEFFECT); // "Performed DropEffect"
    m_Format_LogicalPerformedDropEffect =
        RegisterClipboardFormat(CFSTR_LOGICALPERFORMEDDROPEFFECT); // "Logical Performed DropEffect"
    m_Format_DisableDragText = RegisterClipboardFormat(TEXT("DisableDragText"));
    m_Format_IsShowingLayered = RegisterClipboardFormat(TEXT("IsShowingLayered"));
    m_Format_IsShowingText = RegisterClipboardFormat(TEXT("IsShowingText"));
    m_Format_DropDescription = RegisterClipboardFormat(TEXT("DropDescription"));
    m_Format_TargetCLSID = RegisterClipboardFormat(CFSTR_TARGETCLSID);

    m_PerformedDropEffect = 0;
    m_LogicalPerformedDropEffect = 0;

    m_PerformedDropEffect_WasSet = false;
    m_LogicalPerformedDropEffect_WasSet = false;
}
Z7_COMWF_B CDataObject::SetData(LPFORMATETC etc, STGMEDIUM *medium, BOOL release) {
    try {
        const HRESULT hres = SetData2(etc, medium);
        // PrintFormat(release ? "SetData RELEASE=TRUE" : "SetData RELEASE=FALSE" , etc->cfFormat);
        if (release) {
            /*
            const DWORD tymed = medium->tymed;
            IUnknown *pUnkForRelease = medium->pUnkForRelease;
            */
            // medium->tymed = NULL; // for debug
            // return E_NOTIMPL;  // for debug
            ReleaseStgMedium(medium);
            /* ReleaseStgMedium() will change STGMEDIUM::tymed to (TYMED_NULL = 0).
               but we also can clear (medium.hGlobal = NULL),
               to prevent some incorrect releasing, if the caller will try to release the data  */
            /*
            if (medium->tymed == TYMED_NULL && tymed == TYMED_HGLOBAL && !pUnkForRelease)
              medium->hGlobal = NULL;
            */
            // do we need return S_OK; for (tymed != TYMED_HGLOBAL) cases ?
            /* we return S_OK here to shows that we take ownership of the data in (medium),
               so the caller will not try to release (medium) */
            return S_OK; // to be more compatible with Win10-Explorer and DOCs.
        }
        return hres;
    } catch (...) {
        return E_FAIL;
    }
}

HRESULT CDataObject::SetData2(const FORMATETC *etc, const STGMEDIUM *medium) {
    // PRF3("== CDataObject::SetData()");

    HRESULT hres = S_OK;

    if (etc->cfFormat == 0)
        return DV_E_FORMATETC;
    if (etc->tymed != TYMED_HGLOBAL)
        return E_NOTIMPL; // DV_E_TYMED;
    if (etc->dwAspect != DVASPECT_CONTENT)
        return E_NOTIMPL; // DV_E_DVASPECT;
    if (medium->tymed != TYMED_HGLOBAL)
        return E_NOTIMPL; // DV_E_TYMED;

    if (!medium->hGlobal)
        return S_OK;

    if (etc->cfFormat == m_Format_7zip_SetTargetFolder) {
        model_->DestDirPrefix_FromTarget.Empty();
        model_->m_DestDirPrefix_FromTarget_WasSet = true;
    } else if (etc->cfFormat == m_Format_7zip_SetTransfer)
        model_->m_Transfer_WasSet = false;

    const size_t size = GlobalSize(medium->hGlobal);
    // GlobalLock() can return NULL, if memory block has a zero size
    if (size == 0)
        return S_OK;
    NWindows::NMemory::CGlobalLock lock(medium->hGlobal);
    const void *src = lock.GetPointer();
    if (!src)
        return E_FAIL;

    if (etc->cfFormat == m_Format_7zip_SetTargetFolder) {
        /* this is our registered k_Format_7zip_SetTargetFolder format.
           so it's call from 7-zip's CDropTarget */
        /* 7-zip's CDropTarget calls SetData() for m_Format_7zip_SetTargetFolder
           with (release == FALSE) */
        const size_t num = size / sizeof(wchar_t);
        if (size != num * sizeof(wchar_t))
            return E_FAIL;
        // if (num == 0) return S_OK;
        // GlobalLock() can return NULL, if memory block has a zero-byte size
        const wchar_t *s = (const wchar_t *)src;
        UString &dest = model_->DestDirPrefix_FromTarget;
        for (size_t i = 0; i < num; i++) {
            const wchar_t c = s[i];
            if (c == 0)
                break;
            dest += c;
        }
        // PRF_(PrintFormat_AndData("SetData", etc->cfFormat, src, size))

    } else if (etc->cfFormat == m_Format_7zip_SetTransfer) {
        /* 7-zip's CDropTarget calls SetData() for m_Format_7zip_SetTransfer
           with (release == FALSE) */
        if (size < sizeof(CDataObject_SetTransfer))
            return E_FAIL;
        const CDataObject_SetTransfer *t = (const CDataObject_SetTransfer *)src;
        if (!t->Check())
            return E_FAIL;
        model_->SetTransfer(ToTyped(t->Target));
    } else {
        // SetData() from Explorer Target:
        if (etc->cfFormat == m_Format_PerformedDropEffect) {
            m_PerformedDropEffect_WasSet = false;
            if (size == sizeof(DWORD)) {
                m_PerformedDropEffect = *(const DWORD *)src;
                m_PerformedDropEffect_WasSet = true;
            }
        } else if (etc->cfFormat == m_Format_LogicalPerformedDropEffect) {
            m_LogicalPerformedDropEffect_WasSet = false;
            if (size == sizeof(DWORD)) {
                m_LogicalPerformedDropEffect = *(const DWORD *)src;
                m_LogicalPerformedDropEffect_WasSet = true;
            }
        } else if (etc->cfFormat == m_Format_DropDescription) {
            // drop description contains only name of dest folder without full path

        } else if (etc->cfFormat == m_Format_TargetCLSID) {
            // it's called after call QueryContinueDrag() (keyState & MK_LBUTTON) == 0
            // Shell File System Folder (explorer) guid: F3364BA0-65B9-11CE-A9BA-00AA004AE837

        } else if (etc->cfFormat == m_Format_DisableDragText) {
            // (size == 4) (UInt32 value)
            //    value==0 : if drag to folder item or folder
            //    value==1 : if drag to file or non list_view */
        } else if (etc->cfFormat == m_Format_IsShowingLayered ||
                   etc->cfFormat == m_Format_IsShowingText) {
            // (size == 4) (UInt32 value) value==0 :
        } else
            hres = DV_E_FORMATETC;
        // hres = E_NOTIMPL; // for debug
        // hres = DV_E_FORMATETC; // for debug
    }

    return hres;
}

static HGLOBAL DuplicateGlobalMem(HGLOBAL srcGlobal) {
    /* GlobalSize() returns 0: If the specified handle
       is not valid or if the object has been discarded */
    const SIZE_T size = GlobalSize(srcGlobal);
    if (size == 0)
        return NULL;
    // GlobalLock() can return NULL, if memory block has a zero-byte size
    const void *src = GlobalLock(srcGlobal);
    if (!src)
        return NULL;
    HGLOBAL destGlobal = GlobalAlloc(GHND | GMEM_SHARE, size);
    if (destGlobal) {
        void *dest = GlobalLock(destGlobal);
        if (!dest) {
            GlobalFree(destGlobal);
            destGlobal = NULL;
        } else {
            memcpy(dest, src, size);
            GlobalUnlock(destGlobal);
        }
    }
    GlobalUnlock(srcGlobal);
    return destGlobal;
}

static bool Medium_CopyFrom(LPSTGMEDIUM medium, const void *data, size_t size) {
    medium->tymed = TYMED_NULL;
    medium->pUnkForRelease = NULL;
    medium->hGlobal = NULL;
    const HGLOBAL global = GlobalAlloc(GHND | GMEM_SHARE, size);
    if (!global)
        return false;
    void *dest = GlobalLock(global);
    if (!dest) {
        GlobalFree(global);
        return false;
    }
    memcpy(dest, data, size);
    GlobalUnlock(global);
    medium->hGlobal = global;
    medium->tymed = TYMED_HGLOBAL;
    return true;
}

Z7_COMWF_B CDataObject::GetData(LPFORMATETC etc, LPSTGMEDIUM medium) {
    try {

        medium->tymed = TYMED_NULL;
        medium->pUnkForRelease = NULL;
        medium->hGlobal = NULL;

        if (model_->NeedCall_Copy && !model_->Copy_WasCalled) {
            // Original CopyFromPanelTo_Folder restores OLE capture before a
            // password/progress modal. Qt supplies the source window handle.
            if (model_->Panel)
                if (QWidget *window = model_->Panel->GetParent())
                    ::SetCapture(reinterpret_cast<HWND>(window->winId()));
            model_->CopyFromPanelTo_Folder();
        }

        // PRF3("+ CDataObject::GetData");
        // PrintFormat(etc->cfFormat);
        HGLOBAL global;
        RINOK(QueryGetData(etc))
        if (etc->cfFormat == CF_HDROP) {
            RINOK(model_->Copy_HRESULT)
        }

        /*
        if (etc->cfFormat == m_Format_FileOpFlags)
          global = m_hGlobal_FileOpFlags;
        else if (etc->cfFormat == m_Format_PreferredDropEffect)
        {
          // Explorer requests PreferredDropEffect only if Move/Copy selection is possible:
          //   Shift is not pressed and Ctrl is not pressed

          global = m_hGlobal_PreferredDropEffect;
        }
        else
        */
        if (etc->cfFormat == m_Etc.cfFormat) // CF_HDROP
            global = model_->UsePreGlobal ? m_hGlobal_HDROP_Pre : m_hGlobal_HDROP_Final;
        else if (etc->cfFormat == m_Format_7zip_GetTransfer) {
            CDataObject_GetTransfer transfer;
            transfer.Flags = model_->GetTransferFlags();

            if (Medium_CopyFrom(medium, &transfer, sizeof(transfer)))
                return S_OK;
            return E_OUTOFMEMORY;
        } else
            return DV_E_FORMATETC;

        if (!global)
            return DV_E_FORMATETC;
        medium->tymed = m_Etc.tymed;
        medium->hGlobal = DuplicateGlobalMem(global);
        if (!medium->hGlobal)
            return E_OUTOFMEMORY;
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

Z7_COMWF_B CDataObject::GetDataHere(LPFORMATETC /* etc */, LPSTGMEDIUM /* medium */) {

    // Seems Windows doesn't call it, so we will not implement it.
    return E_UNEXPECTED;
}

/*
  IDataObject::QueryGetData() Determines whether the data object is capable of
  rendering the data as specified. Objects attempting a paste or drop
  operation can call this method before calling IDataObject::GetData
  to get an indication of whether the operation may be successful.

  The client of a data object calls QueryGetData to determine whether
  passing the specified FORMATETC structure to a subsequent call to
  IDataObject::GetData is likely to be successful.

  we check Try_QueryGetData with CF_HDROP
*/

Z7_COMWF_B CDataObject::QueryGetData(LPFORMATETC etc) {

    if (etc->cfFormat == m_Etc.cfFormat // CF_HDROP
        || etc->cfFormat == m_Format_7zip_GetTransfer
        // || (etc->cfFormat == m_Format_FileOpFlags && (HGLOBAL)m_hGlobal_FileOpFlags)
        // || (etc->cfFormat == m_Format_PreferredDropEffect &&
        // (HGLOBAL)m_hGlobal_PreferredDropEffect)
    ) {
    } else
        return DV_E_FORMATETC;
    if (etc->dwAspect != m_Etc.dwAspect)
        return DV_E_DVASPECT;
    /* GetData(): It is possible to specify more than one medium by using the Boolean OR
       operator, allowing the method to choose the best medium among those specified. */
    if ((etc->tymed & m_Etc.tymed) == 0)
        return DV_E_TYMED;
    return S_OK;
}

Z7_COMWF_B CDataObject::EnumFormatEtc(DWORD direction, LPENUMFORMATETC FAR *enumFormatEtc) {
    // we don't enumerate for DATADIR_SET. Seems it can work without it.
    if (direction != DATADIR_GET)
        return E_NOTIMPL;
    // we don't enumerate for m_Format_FileOpFlags also. Seems it can work without it.
    return CreateEnumFormatEtc(1, &m_Etc, enumFormatEtc);
}

#define INIT_FORMATETC_HGLOBAL(type) {(type), NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}

static bool DataObject_GetData_GetTransfer(IDataObject *dataObject, UINT a_Format_7zip_GetTransfer,
                                           CDataObject_GetTransfer &transfer) {
    FORMATETC etc = INIT_FORMATETC_HGLOBAL((CLIPFORMAT)a_Format_7zip_GetTransfer);
    NCOM::CStgMedium medium;
    const HRESULT res = dataObject->GetData(&etc, &medium);
    if (res != S_OK)
        return false;
    if (medium.tymed != TYMED_HGLOBAL)
        return false;
    const size_t size = GlobalSize(medium.hGlobal);
    if (size < sizeof(transfer))
        return false;
    NMemory::CGlobalLock dropLock(medium.hGlobal);
    const CDataObject_GetTransfer *t = (const CDataObject_GetTransfer *)dropLock.GetPointer();
    if (!t)
        return false;
    if (!t->Check()) // isSetData
        return false;
    transfer = *t;
    return true;
}

class CNativeDragData final : public IQtNativeDragData {
    CMyComPtr<IDataObject> data_;
    bool Send(UINT format, const void *bytes, size_t size, BOOL release) {
        if (!format)
            return false;
        FORMATETC etc = {(CLIPFORMAT)format, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium;
        if (!Medium_CopyFrom(&medium, bytes, size))
            return false;
        const HRESULT hres = data_->SetData(&etc, &medium, release);
        // SetTarget/SetTransfer use FALSE; Explorer performed-effect requires TRUE.
        // On error, ownership did not transfer. A successful TRUE call may leave
        // a stale handle, so never release that successful medium a second time.
        if (!release || hres != S_OK)
            ReleaseStgMedium(&medium);
        return hres == S_OK;
    }

  public:
    explicit CNativeDragData(IDataObject *data) : data_(data) {}
    HRESULT GetNames(UStringVector &names) override {
        return NShell::DataObject_GetData_HDROP_or_IDLIST_Names(data_, names);
    }
    bool SetTargetFolder(const UString &path) override {
        return Send(RegisterClipboardFormat(k_Format_7zip_SetTargetFolder), path.Ptr(),
                    (path.Len() + 1) * sizeof(wchar_t), FALSE);
    }
    bool SetTransfer(const ::CTargetTransferInfo &info) override {
        CDataObject_SetTransfer transfer;
        transfer.Init();
        transfer.Target = ToNative(info);
        return Send(RegisterClipboardFormat(k_Format_7zip_SetTransfer), &transfer, sizeof(transfer),
                    FALSE);
    }
    bool ReadTransferFlags(UInt32 &flags) override {
        CDataObject_GetTransfer transfer;
        if (!DataObject_GetData_GetTransfer(
                data_, RegisterClipboardFormat(k_Format_7zip_GetTransfer), transfer))
            return false;
        flags = transfer.Flags;
        return true;
    }
    void SendPerformedEffect(DWORD effect) override {
        Send(RegisterClipboardFormat(CFSTR_PERFORMEDDROPEFFECT), &effect, sizeof(effect), TRUE);
    }
};
class CDropSource final : public IDropSource, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1_MT(IDropSource)
    ::CDropSource &source_;

  public:
    explicit CDropSource(::CDropSource &source) : source_(source) {}
    STDMETHOD(QueryContinueDrag)(BOOL escapePressed, DWORD keyState) override {
        return source_.QueryContinueDrag(escapePressed, keyState);
    }
    STDMETHOD(GiveFeedback)(DWORD effect) override { return source_.GiveFeedback(effect); }
};
} // namespace NPanelDragNative

HRESULT CreatePanelDragDataObject(const std::shared_ptr<CDataObject> &model, IDataObject **out) {
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!model)
        return E_INVALIDARG;
    auto *native = new NPanelDragNative::CDataObject(model);
    CMyComPtr<IDataObject> data = native;
    RINOK(native->InitNames())
    *out = data.Detach();
    return S_OK;
}
std::shared_ptr<IQtNativeDragData> ReadPanelDragDataObject(IDataObject *data) {
    return data ? std::make_shared<NPanelDragNative::CNativeDragData>(data) : nullptr;
}
HRESULT DoNativePanelDrag(CDataObject &model, CDropSource &source, DWORD allowed, DWORD &effect) {
    const auto owner = model.weak_from_this().lock();
    if (!owner)
        return E_INVALIDARG;
    CMyComPtr<IDataObject> data;
    RINOK(CreatePanelDragDataObject(owner, &data))
    if (!model.BeginExternalReceiver()) {
        model.Copy_HRESULT = E_FAIL;
        return DRAGDROP_S_CANCEL;
    }
    CMyComPtr<IDropSource> dropSource = new NPanelDragNative::CDropSource(source);
    const HRESULT result = ::DoDragDrop(data, dropSource, allowed, &effect);
    model.CompleteExternalDrag(result == DRAGDROP_S_CANCEL);
    return result;
}
#endif
