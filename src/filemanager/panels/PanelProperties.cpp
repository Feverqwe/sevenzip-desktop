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
#include "archive/agent/Agent.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "archive/opening/OpenArchive.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/properties/qtpropertyreport.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <limits>

static void AddReportedTotal(UInt64 &total, UInt64 value, UInt32 property,
                             QtPropertyReport &report) {
    if (value > std::numeric_limits<UInt64>::max() - total)
        report.overflowedTotals.insert(property);
    total += value;
}

static const char *const kSeparator = "------------------------";
static const char *const kSeparatorSmall = "----------------";

static void AddListAscii(QtPropertyReport &dialog, const char *s) {
    dialog.Strings.Add((UString)s);
    dialog.Values.AddNew();
}

static void AddSeparator(QtPropertyReport &dialog) { AddListAscii(dialog, kSeparator); }
static void AddSeparatorSmall(QtPropertyReport &dialog) { AddListAscii(dialog, kSeparatorSmall); }

static void AddPropertyPair(const UString &name, const UString &val, QtPropertyReport &dialog) {
    dialog.AddValue(name, val);
}

static void AddPropertyString(PROPID propID, const wchar_t *nameBSTR,
                              const NWindows::NCOM::CPropVariant &prop, QtPropertyReport &dialog) {
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
            auto availability = QtPropertyReport::Availability::Known;
            if (dialog.items.size() > 1 && !dialog.groups.isEmpty() &&
                dialog.groups.last().scope == QtPropertyReport::Scope::Selection)
                availability = dialog.overflowedTotals.contains(propID)
                                   ? QtPropertyReport::Availability::Unknown
                                   : QtPropertyReport::Availability::Partial;
            dialog.nativeValues.insert(row, {propID, prop, availability});
            if (propID == kpidErrorFlags || propID == kpidWarningFlags)
                dialog.QtBindArchiveFlagsValue(row, GetOpenArcErrorFlags(prop));
            else if (propID == kpidError || propID == kpidWarning) {
                dialog.QtBindArchiveDiagnosticValue(row, val);
                dialog.Values[row] = QtArchiveDiagnosticText(val);
            }
        }
    }
}

static void AddPropertyString(PROPID propID, UInt64 val, QtPropertyReport &dialog) {
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
    // Unix has no Windows shell property-sheet provider. Snapshot the native
    // collector before showing independent information windows.
    CRecordVector<UInt32> operatedIndices;
    Get_ItemIndices_Operated(operatedIndices);
    QtPropertyReport message;
    Properties(operatedIndices, message);
    ShowProperties(std::move(message));
}

void CPanel::ShowProperties(QtPropertyReport report) {
    auto *window =
        new QtPropertiesWindow(std::move(report), GetParent(), [panel = QPointer<CPanel>(this)] {
            return panel ? panel->_selectionList.FocusTarget() : nullptr;
        });
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

void CPanel::Properties(const CRecordVector<UInt32> &operatedIndices,
                        QtPropertyReport &message) const {
    CMyComPtr<IGetFolderArcProps> getFolderArcProps;
    _folder.QueryInterface(IID_IGetFolderArcProps, &getFolderArcProps);
    message.location = ToQString(_currentFolderPrefix);
    FOR_VECTOR(i, operatedIndices) {
        const auto index = operatedIndices[i];
        QtPropertyReport::Item item;
        NWindows::NCOM::CPropVariant name, directory, prefix;
        if (_folder->GetProperty(index, kpidName, &name) == S_OK && name.vt == VT_BSTR)
            item.name = ToQString(UString(name.bstrVal));
        if (_folder->GetProperty(index, kpidIsDir, &directory) == S_OK && directory.vt == VT_BOOL)
            item.directory = VARIANT_BOOLToBool(directory.boolVal);
        if (_folder->GetProperty(index, kpidPrefix, &prefix) == S_OK && prefix.vt == VT_BSTR)
            item.prefix = ToQString(UString(prefix.bstrVal));
        message.items.append(std::move(item));
    }
    message.BeginGroup(QtPropertyReport::Scope::Selection);
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
                    message.QtBindPropertyName(message.Strings.Size() - 1, propID, name);
                    message.rawValues.insert(
                        message.Strings.Size() - 1,
                        {propID, propType, dataSize,
                         dataSize <= 256
                             ? QByteArray(static_cast<const char *>(data), int(dataSize))
                             : QByteArray{}});
                }
            }
        }
        AddSeparator(message);
    } else if (operatedIndices.Size() >= 1) {
        UInt64 packSize = 0;
        UInt64 unpackSize = 0;
        UInt64 numFiles = 0;
        UInt64 numDirs = 0;
        bool completeFileCount = true;
        bool completeDirCount = true;
        bool completeSize = true;
        bool hasReportedSize = false;
        bool hasPackedSize = false;
        const bool exactFolderSizes =
            IsFSFolder() || (!GetFlatMode() && dynamic_cast<CAgentFolder *>(_folder.Interface()));
        const auto addFolderCount = [&](UInt32 index, PROPID id, UInt64 &total, bool &complete) {
            NWindows::NCOM::CPropVariant value;
            UInt64 count = 0;
            if (_folder->GetProperty(index, id, &value) == S_OK &&
                ConvertPropVariantToUInt64(value, count))
                AddReportedTotal(total, count, id, message);
            else
                complete = false;
        };
        FOR_VECTOR(i, operatedIndices) {
            const UInt32 index = operatedIndices[i];
            NWindows::NCOM::CPropVariant size;
            UInt64 bytes = 0;
            if (_folder->GetProperty(index, kpidSize, &size) == S_OK &&
                ConvertPropVariantToUInt64(size, bytes)) {
                hasReportedSize = true;
                AddReportedTotal(unpackSize, bytes, kpidSize, message);
            } else {
                completeSize = false;
                const UInt64 fallback = GetItemSize(index);
                hasReportedSize |= fallback != 0;
                AddReportedTotal(unpackSize, fallback, kpidSize, message);
            }
            NWindows::NCOM::CPropVariant packed;
            if (_folder->GetProperty(index, kpidPackSize, &packed) == S_OK &&
                ConvertPropVariantToUInt64(packed, bytes)) {
                hasPackedSize = true;
                AddReportedTotal(packSize, bytes, kpidPackSize, message);
            }
            if (IsItem_Folder(index)) {
                if (!exactFolderSizes)
                    completeSize = false;
                AddReportedTotal(numDirs, 1, kpidNumSubDirs, message);
                addFolderCount(index, kpidNumSubDirs, numDirs, completeDirCount);
                addFolderCount(index, kpidNumSubFiles, numFiles, completeFileCount);
            } else
                AddReportedTotal(numFiles, 1, kpidNumSubFiles, message);
        }
        {
            const std::wstring selected =
                QtSelectedItemsText(int(operatedIndices.Size())).toStdWString();
            AddPropertyPair(L"", UString(selected.c_str()), message);
            message.selectionSummaryRow = message.Strings.Size() - 1;
        }
        const auto addCountRow = [&](PROPID id, UInt64 total, bool complete) {
            if (total == 0 && !message.overflowedTotals.contains(id))
                return;
            AddPropertyString(id, total, message);
            if (complete && !message.overflowedTotals.contains(id)) {
                auto row = message.nativeValues.find(message.Strings.Size() - 1);
                if (row != message.nativeValues.end() && row->id == id)
                    row->availability = QtPropertyReport::Availability::Known;
            }
        };
        addCountRow(kpidNumSubDirs, numDirs, completeDirCount);
        addCountRow(kpidNumSubFiles, numFiles, completeFileCount);
        AddPropertyString(kpidSize, unpackSize, message);
        auto size = message.nativeValues.find(message.Strings.Size() - 1);
        if (size != message.nativeValues.end() && size->id == kpidSize) {
            if (!hasReportedSize)
                size->availability = QtPropertyReport::Availability::Unknown;
            else if (completeSize && !message.overflowedTotals.contains(kpidSize))
                size->availability = QtPropertyReport::Availability::Known;
        }
        AddPropertyString(kpidPackSize, packSize, message);
        if (!hasPackedSize) {
            auto packed = message.nativeValues.find(message.Strings.Size() - 1);
            if (packed != message.nativeValues.end() && packed->id == kpidPackSize)
                packed->availability = QtPropertyReport::Availability::Unknown;
        }
        AddSeparator(message);
    }
    message.BeginGroup(QtPropertyReport::Scope::Folder);
    {
        NWindows::NCOM::CPropVariant prop;
        if (_folder->GetFolderProperty(kpidPath, &prop) == S_OK) {
            const auto before = message.Strings.Size();
            AddPropertyString(kpidName, L"Path", prop, message);
            if (message.Strings.Size() > before)
                message.nativeValues.insert(
                    message.Strings.Size() - 1,
                    {kpidPath, prop, QtPropertyReport::Availability::Known});
            if (message.location.isEmpty() && prop.vt == VT_BSTR)
                message.location = ToQString(UString(prop.bstrVal));
        }
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
                        message.BeginGroup(QtPropertyReport::Scope::Archive, level);
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
                        message.BeginGroup(QtPropertyReport::Scope::ArchiveLink, level);
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
                        message.BeginGroup(QtPropertyReport::Scope::UnopenedArchive, numLevels);
                        needSep = false;
                    }
                    AddPropertyString(propID, name, prop, message);
                }
            }
        }
    }
    message.Title = LangString(IDS_PROPERTIES);
    message.TitleID = IDS_PROPERTIES;
    message.NumColumns = 2;
}
