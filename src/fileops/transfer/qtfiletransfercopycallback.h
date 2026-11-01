#pragma once

#include "7zip/UI/FileManager/IFolder.h"
#include "Common/MyCom.h"
#include "fileops/overwrite/overwritequestion.h"
#include <QStringList>
#include <functional>

// Bridges the source FSFolderCopy AskWrite/progress contract to the staged
// filesystem transaction. It holds no widget or archive backend state.
class CFileTransferCopyCallback final : public IFolderOperationsExtractCallback,
                                        public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderOperationsExtractCallback)

  public:
    using Decide =
        std::function<HRESULT(const OverwriteQuestion::File &, const QString &, QString &, bool &)>;
    CFileTransferCopyCallback(Decide decide, std::function<bool(quint64)> advance,
                              std::function<void(const QString &)> currentFile);

    Z7_COM7F_IMF(SetTotal(UInt64 total)) override;
    Z7_COM7F_IMF(SetCompleted(const UInt64 *completed)) override;
    Z7_COM7F_IMF(SetNumFiles(UInt64 numFiles)) override;
    Z7_COM7F_IMF(SetCurrentFilePath(const wchar_t *path)) override;
    Z7_COM7F_IMF(ShowMessage(const wchar_t *message)) override;
    Z7_COM7F_IMF(AskWrite(const wchar_t *srcPath, Int32 srcIsFolder, const FILETIME *srcTime,
                          const UInt64 *srcSize, const wchar_t *destPath, BSTR *destPathResult,
                          Int32 *writeAnswer))
    override;

    QStringList writtenSources;
    QString errorMessage;

  private:
    Decide decide_;
    std::function<bool(quint64)> advance_;
    std::function<void(const QString &)> currentFile_;
    quint64 previous_ = 0;
};
