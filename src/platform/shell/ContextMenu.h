// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Shared source command owner with native Windows COM interfaces.
#pragma once
#include "archive/operations/ZipRegistry.h"
#include "platform/shell/Shell.h"
#include "platform/shell/qtcontextmenuservices.h"
#include "platform/shell/qtshellmenupresentation.h"
#include <QString>
#ifdef _WIN32
#include "7zip/UI/Explorer/MyExplorerCommand.h"
#include "7zip/UI/FileManager/MyCom2.h"
#endif
enum enum_CtxCommandType {
    CtxCommandType_Normal,
    CtxCommandType_OpenRoot,
    CtxCommandType_OpenChild,
    CtxCommandType_CrcRoot,
    CtxCommandType_CrcChild
};
class CZipContextMenu
#ifdef _WIN32
    final : public IContextMenu,
            public IShellExtInit,
            public IExplorerCommand,
            public IEnumExplorerCommand,
            public CMyUnknownImp
#endif
{
#ifdef _WIN32
    Z7_COM_UNKNOWN_IMP_4_MT(IContextMenu, IShellExtInit, IExplorerCommand, IEnumExplorerCommand)
#endif
  public:
    enum enum_CommandInternalID {
        kCommandNULL,
        kOpen,
        kExtract,
        kExtractHere,
        kExtractTo,
        kTest,
        kCompress,
        kCompressEmail,
        kCompressTo7z,
        kCompressTo7zEmail,
        kCompressToZip,
        kCompressToZipEmail,
        kHash_CRC32,
        kHash_CRC64,
        kHash_XXH64,
        kHash_MD5,
        kHash_SHA1,
        kHash_SHA256,
        kHash_SHA384,
        kHash_SHA512,
        kHash_SHA3_256,
        kHash_BLAKE2SP
    };

    struct CCommandMapItem {
        enum_CommandInternalID CommandInternalID;
        UString Verb;
        UString UserString;
        // UString HelpString;
        UString Folder;
        UString ArcName;
        UString ArcType;
        bool IsPopup;
        enum_CtxCommandType CtxCommandType;

        CCommandMapItem() : IsPopup(false), CtxCommandType(CtxCommandType_Normal) {}

        bool IsSubMenu() const {
            return CtxCommandType == CtxCommandType_CrcRoot ||
                   CtxCommandType == CtxCommandType_OpenRoot;
        }
    };

    UStringVector _fileNames;
    NWindows::NShell::CFileAttribs _attribs;

  private:
    bool _isMenuForFM = true;
    bool _fileNames_WereReduced = true;
    bool _dropMode = false;
    UString _dropPath;
    CObjectVector<CCommandMapItem> _commandMap;
    unsigned _bitmap = 0;
    UInt32 _writeZone = UInt32(-1);
    CBoolPair _elimDup;
    QString _settingsPath;
    IQtContextMenuServices *_services;
#ifdef _WIN32
    std::shared_ptr<void> _nativeBitmap;
    std::unique_ptr<IQtContextMenuServices> _ownedServices;
    CObjectVector<CCommandMapItem> _commandMap_Cur;
    bool IsSeparator = false;
    bool IsRoot = true;
    CObjectVector<CMyComPtr<IExplorerCommand>> SubCommands;
    unsigned CurrentSubCommand = 0;
#endif
    void Set_UserString_in_LastCommand(const UString &s) { _commandMap.Back().UserString = s; }
    void FillCommand(enum_CommandInternalID id, UString &mainString, CCommandMapItem &cmi) const;
    void AddCommand(enum_CommandInternalID id, UString &mainString, CCommandMapItem &cmi);
    void AddMapItem_ForSubMenu(const char *verb);

  public:
    CZipContextMenu(const QString &settingsPath, IQtContextMenuServices *services);
#ifdef _WIN32
    CZipContextMenu();
    ~CZipContextMenu();
    void LoadItems(IShellItemArray *items);
#endif
    void Init_For_7zFM() { _isMenuForFM = true; }
    void Init_For_Shell() { _isMenuForFM = false; }
    void SetDropPath(const UString &path) {
        _dropPath = path;
        _dropMode = !path.IsEmpty();
    }
    HRESULT QueryContextMenu(NQtShellMenu::CMenuHandle *menu, unsigned indexMenu, unsigned first,
                             unsigned last, unsigned flags);
    int FindVerb(const UString &verb) const;
    HRESULT InvokeCommand(unsigned commandOffset);
#ifdef _WIN32
    HRESULT STDMETHODCALLTYPE QueryContextMenu(HMENU menu, UINT indexMenu, UINT first, UINT last,
                                               UINT flags);
    HRESULT STDMETHODCALLTYPE Initialize(LPCITEMIDLIST pidlFolder, LPDATAOBJECT dataObject,
                                         HKEY hkeyProgID);
    HRESULT STDMETHODCALLTYPE InvokeCommand(LPCMINVOKECOMMANDINFO commandInfo);
    HRESULT STDMETHODCALLTYPE GetCommandString(UINT_PTR commandOffset, UINT type, UINT *reserved,
                                               LPSTR name, UINT maxCharacters);
    STDMETHOD(GetTitle)(IShellItemArray *, LPWSTR *) override;
    STDMETHOD(GetIcon)(IShellItemArray *, LPWSTR *) override;
    STDMETHOD(GetToolTip)(IShellItemArray *, LPWSTR *) override;
    STDMETHOD(GetCanonicalName)(GUID *) override;
    STDMETHOD(GetState)(IShellItemArray *, BOOL, EXPCMDSTATE *) override;
    STDMETHOD(Invoke)(IShellItemArray *, IBindCtx *) override;
    STDMETHOD(GetFlags)(EXPCMDFLAGS *) override;
    STDMETHOD(EnumSubCommands)(IEnumExplorerCommand **) override;
    STDMETHOD(Next)(ULONG, IExplorerCommand **, ULONG *) override;
    STDMETHOD(Skip)(ULONG) override;
    STDMETHOD(Reset)() override;
    STDMETHOD(Clone)(IEnumExplorerCommand **) override;
#endif
    HRESULT InvokeCommandCommon(const CCommandMapItem &cmi);
    const CObjectVector<CCommandMapItem> &Commands() const { return _commandMap; }
};
