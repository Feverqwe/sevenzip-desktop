// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelMenu.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Source item/raw/folder/archive property collection with Qt presentation.
#include "7zip/PropID.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/IntToString.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "archive/opening/OpenArchive.h"
#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/ExtractCallback.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/diagnostics/qtdiagnostictext.h"

static const char *const kSeparator = "------------------------";
static const char *const kSeparatorSmall = "----------------";

static void AddListAscii(CListViewDialog &dialog, const char *s) {
    dialog.Strings.Add((UString)s);
    dialog.Values.AddNew();
}

static void AddSeparator(CListViewDialog &dialog) { AddListAscii(dialog, kSeparator); }
static void AddSeparatorSmall(CListViewDialog &dialog) { AddListAscii(dialog, kSeparatorSmall); }

static void AddPropertyPair(const UString &name, const UString &val, CListViewDialog &dialog) {
    dialog.Strings.Add(name);
    dialog.Values.Add(val);
}

static void AddPropertyString(PROPID propID, const wchar_t *nameBSTR,
                              const NWindows::NCOM::CPropVariant &prop, CListViewDialog &dialog) {
    if (prop.vt != VT_EMPTY) {
        UString val;

        if (propID == kpidErrorFlags || propID == kpidWarningFlags) {
            UInt32 flags = GetOpenArcErrorFlags(prop);
            if (flags == 0)
                return;
            if (flags != 0)
                val = GetOpenArcErrorMessage(flags);
        }

        if (val.IsEmpty()) {
            if ((prop.vt == VT_UI8 || prop.vt == VT_UI4 || prop.vt == VT_UI2) &&
                IsSizeProp(propID)) {
                UInt64 v = 0;
                ConvertPropVariantToUInt64(prop, v);
                val = ToUString(ConvertSizeToString(v));
            } else
                ConvertPropertyToString2(val, prop, propID, 9);
        }

        if (!val.IsEmpty()) {
            if (propID == kpidErrorType) {
                const char *name = QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Open WARNING:");
                const char *value = QT_TRANSLATE_NOOP(
                    "SevenZipDiagnostics", "Cannot open the file as expected archive type");
                AddPropertyPair(QtDiagnosticText(name), QtDiagnosticText(value), dialog);
                dialog.QtBindDiagnosticRow(dialog.Strings.Size() - 1, name, value);
            }
            AddPropertyPair(GetNameOfProperty(propID, nameBSTR), val, dialog);
            dialog.QtBindPropertyName(dialog.Strings.Size() - 1, propID, nameBSTR);
            const unsigned row = dialog.Strings.Size() - 1;
            if (propID == kpidErrorFlags || propID == kpidWarningFlags)
                dialog.QtBindArchiveFlagsValue(row, GetOpenArcErrorFlags(prop));
            else if (propID == kpidError || propID == kpidWarning) {
                dialog.QtBindArchiveDiagnosticValue(row, val);
                dialog.Values[row] = QtArchiveDiagnosticText(val);
            }
        }
    }
}

static void AddPropertyString(PROPID propID, UInt64 val, CListViewDialog &dialog) {
    NWindows::NCOM::CPropVariant prop = val;
    AddPropertyString(propID, NULL, prop, dialog);
}

static const Byte kSpecProps[] = {kpidPath,       kpidType,    kpidErrorType,    kpidError,
                                  kpidErrorFlags, kpidWarning, kpidWarningFlags, kpidOffset,
                                  kpidPhySize,    kpidTailSize};

void CPanel::Properties() {
    CMyComPtr<IGetFolderArcProps> getFolderArcProps;
    _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
#ifdef _WIN32
    if (!getFolderArcProps) {
        InvokeSystemCommand("properties");
        return;
    }
#endif
    // The existing projection supplies Qt's modal creation around the retained
    // source collector. Unix has no Windows shell property-sheet provider.
    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);
    CListViewDialog message(GetParent());
    Properties(operatedIndices, message);
    message.OnInit();
    message.setObjectName("selectedPropertiesDialog");
    message.exec();
}

void CPanel::Properties(const CRecordVector<UInt32> &operatedIndices,
                        CListViewDialog &message) const {
    CMyComPtr<IGetFolderArcProps> getFolderArcProps;
    _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
    // Public collector for archive metadata and the Unix presentation fallback.
    // The no-argument source entry invokes the native Windows property sheet.
    if (operatedIndices.Size() == 1) {
        UInt32 index = operatedIndices[0];
        UInt32 numProps;
        if (_folder->GetNumberOfProperties(&numProps) == S_OK) {
            for (UInt32 i = 0; i < numProps; i++) {
                CMyComBSTR name;
                PROPID propID;
                VARTYPE varType;
                if (_folder->GetPropertyInfo(i, &name, &propID, &varType) != S_OK)
                    continue;
                NWindows::NCOM::CPropVariant prop;
                if (_folder->GetProperty(index, propID, &prop) != S_OK)
                    continue;
                AddPropertyString(propID, name, prop, message);
            }
        }
        if (_folderRawProps) {
            UInt32 numRawProps = 0;
            // Preserve source partial enumeration; each descriptor/value is checked below.
            _folderRawProps->GetNumRawProps(&numRawProps);
            for (UInt32 i = 0; i < numRawProps; i++) {
                CMyComBSTR name;
                PROPID propID;
                if (_folderRawProps->GetRawPropInfo(i, &name, &propID) != S_OK)
                    continue;
                const void *data;
                UInt32 dataSize;
                UInt32 propType;
                if (_folderRawProps->GetRawProp(index, propID, &data, &dataSize, &propType) != S_OK)
                    continue;
                if (dataSize != 0) {
                    AString s;
                    if (propID == kpidNtSecure)
                        ConvertNtSecureToString((const Byte *)data, dataSize, s);
                    else {
                        const unsigned kMaxDataSize = 1 << 8;
                        if (dataSize > kMaxDataSize) {
                            s += "data:";
                            s.Add_UInt32(dataSize);
                        } else {
                            char temp[kMaxDataSize * 2 + 2];
                            if (dataSize <= 8 && (propID == kpidCRC || propID == kpidChecksum))
                                ConvertDataToHex_Upper(temp, (const Byte *)data, dataSize);
                            else
                                ConvertDataToHex_Lower(temp, (const Byte *)data, dataSize);
                            s += temp;
                        }
                    }
                    AddPropertyPair(GetNameOfProperty(propID, name), (UString)s.Ptr(), message);
                }
            }
        }
        AddSeparator(message);
    } else if (operatedIndices.Size() >= 1) {
        UInt64 packSize = 0;
        UInt64 unpackSize = 0;
        UInt64 numFiles = 0;
        UInt64 numDirs = 0;
        FOR_VECTOR(i, operatedIndices) {
            const UInt32 index = operatedIndices[i];
            unpackSize += GetItemSize(index);
            packSize += GetItem_UInt64Prop(index, kpidPackSize);
            if (IsItem_Folder(index)) {
                numDirs++;
                numDirs += GetItem_UInt64Prop(index, kpidNumSubDirs);
                numFiles += GetItem_UInt64Prop(index, kpidNumSubFiles);
            } else
                numFiles++;
        }
        {
            const std::wstring selected =
                QtSelectedItemsText(int(operatedIndices.Size())).toStdWString();
            AddPropertyPair(L"", UString(selected.c_str()), message);
        }
        if (numDirs != 0)
            AddPropertyString(kpidNumSubDirs, numDirs, message);
        if (numFiles != 0)
            AddPropertyString(kpidNumSubFiles, numFiles, message);
        AddPropertyString(kpidSize, unpackSize, message);
        AddPropertyString(kpidPackSize, packSize, message);
        AddSeparator(message);
    }
    {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetFolderProperty(kpidPath, &prop) == S_OK)
            AddPropertyString(kpidName, L"Path", prop, message);
    }
    CMyComPtr<IFolderProperties> folderProperties;
    _folder.QueryInterface(IID_IFolderProperties, &folderProperties);
    if (folderProperties) {
        UInt32 numProps;
        if (folderProperties->GetNumberOfFolderProperties(&numProps) == S_OK) {
            for (UInt32 i = 0; i < numProps; i++) {
                CMyComBSTR name;
                PROPID propID;
                VARTYPE vt;
                if (folderProperties->GetFolderPropertyInfo(i, &name, &propID, &vt) != S_OK)
                    continue;
                NWindows::NCOM::CPropVariant prop;
                if (_folder->GetFolderProperty(propID, &prop) != S_OK)
                    continue;
                AddPropertyString(propID, name, prop, message);
            }
        }
    }
    if (getFolderArcProps) {
        CMyComPtr<IFolderArcProps> getProps;
        getFolderArcProps->GetFolderArcProps(&getProps);
        if (getProps) {
            UInt32 numLevels;
            if (getProps->GetArcNumLevels(&numLevels) != S_OK)
                numLevels = 0;
            for (UInt32 level2 = 0; level2 < numLevels; level2++) {
                {
                    UInt32 level = numLevels - 1 - level2;
                    UInt32 numProps;
                    if (getProps->GetArcNumProps(level, &numProps) == S_OK) {
                        const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);
                        AddSeparator(message);
                        for (Int32 i = -(int)kNumSpecProps; i < (Int32)numProps; i++) {
                            CMyComBSTR name;
                            PROPID propID;
                            VARTYPE vt;
                            if (i < 0)
                                propID = kSpecProps[i + kNumSpecProps];
                            else if (getProps->GetArcPropInfo(level, (UInt32)i, &name, &propID,
                                                              &vt) != S_OK)
                                continue;
                            NWindows::NCOM::CPropVariant prop;
                            if (getProps->GetArcProp(level, propID, &prop) != S_OK)
                                continue;
                            AddPropertyString(propID, name, prop, message);
                        }
                    }
                }
                if (level2 < numLevels - 1) {
                    const UInt32 level = numLevels - 1 - level2;
                    UInt32 numProps;
                    if (getProps->GetArcNumProps2(level, &numProps) == S_OK) {
                        AddSeparatorSmall(message);
                        for (UInt32 i = 0; i < numProps; i++) {
                            CMyComBSTR name;
                            PROPID propID;
                            VARTYPE vt;
                            if (getProps->GetArcPropInfo2(level, i, &name, &propID, &vt) != S_OK)
                                continue;
                            NWindows::NCOM::CPropVariant prop;
                            if (getProps->GetArcProp2(level, propID, &prop) != S_OK)
                                continue;
                            AddPropertyString(propID, name, prop, message);
                        }
                    }
                }
            }
            {
                bool needSep = true;
                const int kNumSpecProps = Z7_ARRAY_SIZE(kSpecProps);
                for (Int32 i = -(int)kNumSpecProps; i < 0; i++) {
                    CMyComBSTR name;
                    const PROPID propID = kSpecProps[i + kNumSpecProps];
                    NWindows::NCOM::CPropVariant prop;
                    if (getProps->GetArcProp(numLevels, propID, &prop) != S_OK)
                        continue;
                    if (needSep) {
                        AddSeparator(message);
                        AddSeparator(message);
                        needSep = false;
                    }
                    AddPropertyString(propID, name, prop, message);
                }
            }
        }
    }
    message.Title = LangString(IDS_PROPERTIES);
    QtBindLangText(&message, IDS_PROPERTIES);
    message.NumColumns = 2;
}
