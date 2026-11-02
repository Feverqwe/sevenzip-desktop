#include "archive/resources/qtarchivetransferinputs.h"
#include "Common/StringConvert.h"
#include "archive/operations/EnumDirItems.h"
#include <QSet>

HRESULT PrepareQtArchiveTransferInputs(CDirItems &items, const FString &folderPrefix,
                                       const UString &updatePrefix,
                                       const QtArchiveTransferInputs &inputs,
                                       CRecordVector<Byte> &altStreamItems) {
    if (inputs.AltStreamPaths.isEmpty())
        return S_OK;
    const QSet<QString> streams(inputs.AltStreamPaths.cbegin(), inputs.AltStreamPaths.cend());
#ifndef _WIN32
    // Unix enumeration does not discover sibling host:stream files when only
    // the host was selected. Enumerate those confirmed extraction outputs too.
    QSet<QString> enumerated;
    FOR_VECTOR(i, items.Items)
    enumerated.insert(QString::fromWCharArray(fs2us(items.GetPhyPath(i))));
    FStringVector extraNames;
    const UString prefix = fs2us(folderPrefix);
    for (const QString &path : streams) {
        if (!enumerated.contains(path)) {
            const UString physical(path.toStdWString().c_str());
            if (!physical.IsPrefixedBy(prefix))
                return E_INVALIDARG;
            const UString relative(physical.Ptr(prefix.Len()));
            // EnumerateItems2 retains the basename for explicit files. Nested
            // streams must already belong to a selected enumerated directory.
            if (relative.Find(WCHAR_PATH_SEPARATOR) >= 0)
                return E_NOTIMPL;
            extraNames.Add(us2fs(relative));
        }
    }
    if (!extraNames.IsEmpty())
        RINOK(items.EnumerateItems2(folderPrefix, updatePrefix, extraNames, nullptr))
#else
    (void)folderPrefix;
    (void)updatePrefix;
#endif
    auto missing = streams;
    FOR_VECTOR(i, items.Items) {
        const QString path = QString::fromWCharArray(fs2us(items.GetPhyPath(i)));
        altStreamItems.Add(streams.contains(path));
        missing.remove(path);
    }
    return missing.isEmpty() ? S_OK : E_FAIL;
}
