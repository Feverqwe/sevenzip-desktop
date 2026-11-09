// Adapted from 7-Zip CPP/7zip/UI/FileManager/PanelListNotify.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// The integer and property-to-text branches are copied; QString is the Qt text
// boundary; source time precision is owned by the panel.
#include "7zip/PropID.h"
#include "7zip/UI/Common/PropIDUtils.h"
#include "Windows/PropVariant.h"
#include "Windows/PropVariantConv.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/properties/qtpropertyformat.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/formatting/qtregionalformat.h"
#include <QColor>
#include <QItemSelectionModel>
#include <limits>
#include <vector>

void CPanel::OnNotifyActivateItems(Qt::KeyboardModifiers modifiers) {
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    const bool ctrl = modifiers.testFlag(Qt::ControlModifier);
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    if (!shift && alt && !ctrl)
        Properties();
    else
        OpenSelectedItems(!shift || alt || ctrl);
}

void CPanel::OnItemChanged(unsigned index, bool oldSelected, bool newSelected) {
    if (index == (UInt32)-1)
        return;
    // Don't change this code. It works only with such check
    if (oldSelected != newSelected)
        _selectedStatusVector[index] = newSelected;
}

CPanel::FocusState CPanel::GetFocusState() const {
    const int focused = _selectionList.GetFocusedItem();
    if (focused < 0)
        return {};
    return {true, GetRealItemIndex(focused) == UInt32(-1), _selectionList.IsItemSelected(focused)};
}

void CPanel::AttachSelectionView(QAbstractItemView *view,
                                 QtPanelList::SortColumnResolver sortColumn,
                                 std::function<void(int)> editLabel) {
    disconnect(_itemChangedConnection);
    _selectionList.Attach(view, std::move(sortColumn), std::move(editLabel));
    if (view)
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    if (!_selectionList.IsBound())
        return;
    // A pre-populated Qt model is an integration input, unlike native Create.
    EnsureSelectionVectorSize();
    for (int row = 0; row < _selectionList.GetItemCount(); ++row) {
        const unsigned index = GetRealItemIndex(row);
        if (index < _selectedStatusVector.Size())
            OnItemChanged(index, _selectedStatusVector[index], _selectionList.IsItemSelected(row));
    }
    _itemChangedConnection =
        connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                [this](const QItemSelection &selected, const QItemSelection &deselected) {
                    if (!_processNotify || !_enableItemChangeNotify)
                        return;
                    const auto notify = [this](const QItemSelection &changes, bool newSelected) {
                        for (const auto &range : changes) {
                            if (range.left() != 0)
                                continue;
                            for (int row = range.top(); row <= range.bottom(); ++row) {
                                quintptr index;
                                if (_selectionList.GetItemParam(unsigned(row), index) &&
                                    index < _selectedStatusVector.Size())
                                    OnItemChanged(unsigned(index), !newSelected, newSelected);
                            }
                        }
                    };
                    notify(deselected, false);
                    notify(selected, true);
                });
}

// CDDS_ITEMPREPAINT foreground policy; Qt model/delegate supplies draw stages.
QColor CPanel::OnCustomDraw(unsigned realIndex) const {
    if (_markDeletedItems && _thereAreDeletedItems)
        if (IsItem_Deleted(realIndex))
            return QColor(255, 0, 0);
    return {};
}

#define SPACE_REPLACE_CHAR (wchar_t)(0x2423)
#define SPACE_TERMINATOR_CHAR (wchar_t)(0x9C)

CPanelStatus CPanel::Refresh_StatusBar() const {
    // Commands retain their current-item fallback. A selection summary uses
    // only explicit selection/marks, and typed sizes distinguish unknown from zero.
    CPanelStatus status;
    CRecordVector<UInt32> indices;
    Get_ItemIndices_Selected(indices);

    status.selectedCount = int(indices.Size());
    status.totalCount = _selectionList.GetItemCount();
    if (status.totalCount > 0 && GetRealItemIndex(0) == UInt32(-1))
        --status.totalCount;
    if (!indices.IsEmpty()) {
        UInt64 totalSize = 0;
        bool known = bool(_folder);
        FOR_VECTOR(i, indices) {
            NWindows::NCOM::CPropVariant prop;
            if (!known || _folder->GetProperty(indices[i], kpidSize, &prop) != S_OK) {
                known = false;
                break;
            }
            const auto size = QtUnsignedPropertyValue(prop);
            if (!size || *size > std::numeric_limits<UInt64>::max() - totalSize) {
                known = false;
                break;
            }
            totalSize += *size;
        }
        if (known)
            status.selectedSize = totalSize;
    }
    if (_statusPresentation)
        _statusPresentation(status);
    return status;
}

QString CPanel::GetItemText(UInt32 itemIndex, const CPropColumn &property) const {
    if (_dontShowMode)
        return QString();
    const PROPID propID = property.ID;
    if (!_folder || itemIndex == UInt32(-1))
        return propID == kpidName ? QStringLiteral("..") : QString();
    if (property.IsRawProp) {
        if (!_folderRawProps)
            return {};
        const void *data = nullptr;
        UInt32 dataSize = 0;
        UInt32 propType = 0;
        if (_folderRawProps->GetRawProp(itemIndex, propID, &data, &dataSize, &propType) != S_OK ||
            dataSize == 0 || !data)
            return {};
        if (propID == kpidNtReparse) {
            UString s;
            ConvertNtReparseToString(static_cast<const Byte *>(data), dataSize, s);
            if (!s.IsEmpty())
                return ToQString(s);
        } else if (propID == kpidNtSecure) {
            AString s;
            ConvertNtSecureToString(static_cast<const Byte *>(data), dataSize, s);
            if (!s.IsEmpty())
                return QString::fromLatin1(s.Ptr(), s.Len());
        }
        if (dataSize > 64)
            return QStringLiteral("data:%1").arg(dataSize);
        const char *const hex = dataSize <= 8 && (propID == kpidCRC || propID == kpidChecksum)
                                    ? "0123456789ABCDEF"
                                    : "0123456789abcdef";
        QString value;
        value.reserve(int(dataSize) * 2);
        const Byte *bytes = static_cast<const Byte *>(data);
        for (UInt32 i = 0; i < dataSize; ++i) {
            value.append(QLatin1Char(hex[bytes[i] >> 4]));
            value.append(QLatin1Char(hex[bytes[i] & 15]));
        }
        return value;
    }
    if (propID == kpidName && _folderGetItemName) {
        const wchar_t *name = nullptr;
        unsigned nameLen = 0;
        _folderGetItemName->GetItemName(itemIndex, &name, &nameLen);
        if (name) {
            std::vector<wchar_t> buffer(size_t(nameLen) + 2);
            wchar_t *text = buffer.data();
            unsigned dest = 0;
            const unsigned limit = nameLen + 1;

            for (unsigned i = 0; dest < limit;) {
                const wchar_t c = name[i++];
                if (c == 0)
                    break;
                text[dest++] = c;

                if (c != ' ') {
                    if (c != 0x202E) // RLO
                        continue;
                    text[(size_t)dest - 1] = '_';
                    continue;
                }

                if (name[i] != ' ')
                    continue;

                unsigned t = 1;
                for (; name[i + t] == ' '; t++)
                    ;

                if (t >= 4 && dest + 4 < limit) {
                    text[dest++] = '.';
                    text[dest++] = '.';
                    text[dest++] = '.';
                    text[dest++] = ' ';
                    i += t;
                }
            }

            if (dest == 0)
                text[dest++] = '_';

#ifdef _WIN32
            else if (text[(size_t)dest - 1] == ' ') {
                if (dest < limit)
                    text[dest++] = SPACE_TERMINATOR_CHAR;
                else
                    text[dest - 1] = SPACE_REPLACE_CHAR;
            }
#endif

            text[dest] = 0;
            return QString::fromWCharArray(text);
        }
    }
    if (propID == kpidPrefix && _folderGetItemName) {
        const wchar_t *name = nullptr;
        unsigned nameLen = 0;
        _folderGetItemName->GetItemPrefix(itemIndex, &name, &nameLen);
        if (name)
            return QString::fromWCharArray(name);
    }
    NWindows::NCOM::CPropVariant prop;
    const HRESULT result = _folder->GetProperty(itemIndex, propID, &prop);
    if (result != S_OK)
        return QString::fromWCharArray(
            QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Error: ")));
    if (IsByteSizeProp(propID)) {
        if (const auto value = QtUnsignedPropertyValue(prop))
            return QtRegionalFormat::bytes(*value);
        if (prop.vt == VT_EMPTY) {
            NWindows::NCOM::CPropVariant directory;
            if (_folder->GetProperty(itemIndex, kpidIsDir, &directory) == S_OK &&
                directory.vt == VT_BOOL && directory.boolVal != VARIANT_FALSE)
                return {};
        }
        return QtRegionalFormat::bytes(std::nullopt);
    }
    if ((prop.vt == VT_UI8 || prop.vt == VT_UI4 || prop.vt == VT_UI2) && IsSizeProp(propID)) {
        UInt64 value = 0;
        ConvertPropVariantToUInt64(prop, value);
        return ConvertSizeToString(value);
    }
    if (prop.vt == VT_FILETIME ||
        (prop.vt == VT_EMPTY && (property.Type == VT_FILETIME || IsTimestampProp(propID))))
        return QtFormatPropertyTime(prop).display;
    if (prop.vt == VT_BSTR) {
        QString value = QString::fromWCharArray(prop.bstrVal);
        value.replace(QLatin1Char('\n'), QLatin1Char(' '));
        value.replace(QLatin1Char('\r'), QLatin1Char(' '));
        return value;
    }
    char text[64];
    ConvertPropertyToShortString2(text, prop, propID, kTimestampPrintLevel_MIN);
    return QString::fromLatin1(text);
}
