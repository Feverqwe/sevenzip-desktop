// Adapted from 7-Zip CPP/7zip/UI/FileManager/FSFolderCopy.cpp (0766b73).
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.
// Qt supplies portable stage/file services; the source CopyTo, AskWrite,
// recursive copy/move and progress control flow remain in this module.
#include "Common/MyBuffer.h"
#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "Windows/FileName.h"
#include "Windows/TimeUtils.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/native/FileDir.h"
#include "localization/qtlanguageservices.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace NFsFolder {
static const char *const k_CannotCopyDirToAltStream =
    QT_TRANSLATE_NOOP("SevenZipDiagnostics", "Cannot copy folder as alternate stream");

static HRESULT SendMessageError(IFolderOperationsExtractCallback *callback, const wchar_t *message,
                                const FString &fileName) {
    UString s = message;
    s += " : ";
    s += fs2us(fileName);
    return callback->ShowMessage(s);
}

static HRESULT SendMessageError(IFolderOperationsExtractCallback *callback, const char *message,
                                const FString &fileName) {
    return SendMessageError(callback, QtDiagnosticText(message), fileName);
}

static DWORD Return_LastError_or_FAIL() {
    DWORD errorCode = GetLastError();
    if (errorCode == 0)
        errorCode = (DWORD)E_FAIL;
    return errorCode;
}

static UString GetLastErrorMessage() {
    return NWindows::NError::MyFormatMessage(Return_LastError_or_FAIL());
}

HRESULT CCopyStateIO::MyCopyFile(CFSTR inPath, CFSTR outPath, DWORD attrib) {
    using namespace NWindows::NFile;
    ErrorFileIndex = -1;
    ErrorMessage.clear();
    CurrentSize = 0;
    NWindows::NFile::NFind::CFileInfo metadata;
    if (attrib != DWORD(-1) && (!metadata.Find(inPath) || metadata.IsOsSymLink())) {
        ErrorFileIndex = 0;
        return S_OK;
    }
    {
        const size_t kBufSize = 1 << 16;
        CByteArr buf(kBufSize);
        NIO::CInFile inFile;
        NIO::COutFile outFile;
        if (!inFile.Open(inPath)) {
            ErrorFileIndex = 0;
            return S_OK;
        }
        // The process adapter publishes staged files; a stage must never overwrite
        // another file. This adapts upstream Create_ALWAYS to native Create_NEW.
        if (!outFile.Create_NEW(outPath)) {
            ErrorFileIndex = 1;
            return S_OK;
        }
        for (;;) {
            UInt32 num;
#ifdef _WIN32
            if (!inFile.Read(buf, kBufSize, num)) {
#else
            size_t processed = 0;
            const bool read = inFile.ReadFull(buf, kBufSize, processed);
            num = UInt32(processed);
            if (!read) {
#endif
                ErrorFileIndex = 0;
                return S_OK;
            }
            if (num == 0)
                break;
            UInt32 written = 0;
#ifdef _WIN32
            if (!outFile.Write(buf, num, written)) {
#else
            processed = 0;
            const ssize_t writeResult = outFile.write_full(buf, num, processed);
            written = UInt32(processed);
            if (writeResult == -1) {
#endif
                ErrorFileIndex = 1;
                return S_OK;
            }
            if (written != num) {
                ErrorMessage = QCoreApplication::translate("FSFolderCopy", "Write error");
                return S_OK;
            }
            CurrentSize += num;
            if (Progress) {
                const UInt64 completed = StartPos + CurrentSize;
                RINOK(Progress->SetCompleted(&completed))
            }
            if (QtProgress && !QtProgress(StartPos + CurrentSize))
                return E_ABORT;
        }
        if ((attrib != DWORD(-1) &&
             !outFile.SetTime(&metadata.CTime, &metadata.ATime, &metadata.MTime)) ||
            !outFile.Close()) {
            ErrorFileIndex = 1;
            return S_OK;
        }
    }
    // Original INVALID_FILE_ATTRIBUTES suppresses changes to an ADS base file.
    if (attrib != DWORD(-1) && !NDir::SetFileAttrib_PosixHighDetect(outPath, attrib)) {
        ErrorFileIndex = 1;
        return S_OK;
    }
    if (DeleteSrcFile) {
        if (!NDir::DeleteFileAlways(inPath)) {
            ErrorFileIndex = 0;
            return S_OK;
        }
    }
    return S_OK;
}

bool CCopyStateIO::MyCopyFile(const QString &inPath, const QString &outPath,
                              QFileDevice::Permissions attrib) {
    Q_UNUSED(attrib)
    ErrorFileIndex = -1;
    ErrorMessage.clear();
    CurrentSize = 0;
    const FString input = us2fs(QDir::toNativeSeparators(inPath).toStdWString().c_str());
    const FString output = us2fs(QDir::toNativeSeparators(outPath).toStdWString().c_str());
    NWindows::NFile::NFind::CFileInfo metadata;
    if (!metadata.Find(input) || metadata.IsOsSymLink()) {
        ErrorFileIndex = 0;
        return true;
    }
    return MyCopyFile(input, output, metadata.GetWinAttrib()) == S_OK;
}

bool IsDestChild(const QString &src, const QString &dest) {
    const qsizetype len = src.size();
    if (dest.size() < len)
        return false;
    if (dest.size() != len && dest[len] != '/')
        return false;
#ifdef Q_OS_WIN
    return QString::compare(dest.left(len), src, Qt::CaseInsensitive) == 0;
#else
    return QString::compare(dest.left(len), src, Qt::CaseSensitive) == 0;
#endif
}

bool CCopyState::CallProgress() {
    const UInt64 completed = StartPos;
    if (Callback && Callback->SetCompleted(&completed) != S_OK) {
        ErrorMessage = QCoreApplication::translate("FSFolderCopy", "Operation canceled.");
        return false;
    }
    if (!Progress || Progress(StartPos))
        return true;
    ErrorMessage = QCoreApplication::translate("FSFolderCopy", "Operation canceled.");
    return false;
}

DWORD CCopyState::ReportSystemProgress(UInt64 total, UInt64 current) {
    if (total > FileSize) {
        TotalSize += total - FileSize;
        FileSize = total;
        if (Callback)
            ProgressResult = Callback->SetTotal(TotalSize);
    }
    const UInt64 completed = StartPos + current;
    if (ProgressResult == S_OK && Callback)
        ProgressResult = Callback->SetCompleted(&completed);
    if (ProgressResult == S_OK && Progress && !Progress(completed))
        ProgressResult = E_ABORT;
    return ProgressResult == S_OK ? PROGRESS_CONTINUE : PROGRESS_CANCEL;
}

#ifdef _WIN32
static DWORD CALLBACK CopyProgressRoutine(LARGE_INTEGER totalFileSize,
                                          LARGE_INTEGER totalBytesTransferred,
                                          LARGE_INTEGER /* streamSize */,
                                          LARGE_INTEGER /* streamBytesTransferred */,
                                          DWORD /* streamNumber */, DWORD /* callbackReason */,
                                          HANDLE /* source */, HANDLE /* destination */,
                                          LPVOID data) {
    return static_cast<CCopyState *>(data)->ReportSystemProgress(
        UInt64(totalFileSize.QuadPart), UInt64(totalBytesTransferred.QuadPart));
}

bool CCopyState::CopyFile_NT(const wchar_t *oldFile, const wchar_t *newFile) {
    BOOL cancelFlag = FALSE;
    return BOOLToBool(::CopyFileExW(oldFile, newFile, CopyProgressRoutine, this, &cancelFlag,
                                    COPY_FILE_FAIL_IF_EXISTS));
}

bool CCopyState::CopyFile_Sys(CFSTR oldFile, CFSTR newFile) {
    using namespace NWindows::NFile::NName;
    IF_USE_MAIN_PATH_2(oldFile, newFile) {
        if (CopyFile_NT(fs2us(oldFile), fs2us(newFile)))
            return true;
    }
#ifdef Z7_LONG_PATH
    if (USE_SUPER_PATH_2) {
        if (ProgressResult != S_OK)
            return false;
        UString superPathOld, superPathNew;
        if (!GetSuperPaths(oldFile, newFile, superPathOld, superPathNew, USE_MAIN_PATH_2))
            return false;
        if (CopyFile_NT(superPathOld, superPathNew))
            return true;
    }
#endif
    return false;
}
#endif

bool CCopyState::CopyFile_Sys(const QString &oldFile, const QString &newFile,
                              QFileDevice::Permissions attrib) {
#ifdef _WIN32
    Q_UNUSED(attrib)
    const std::wstring oldWide = QDir::toNativeSeparators(oldFile).toStdWString();
    const std::wstring newWide = QDir::toNativeSeparators(newFile).toStdWString();
    FileSize = quint64(QFileInfo(oldFile).size());
    ProgressResult = S_OK;
    const bool copied = CopyFile_Sys(us2fs(oldWide.c_str()), us2fs(newWide.c_str()));
    if (copied) {
        StartPos += FileSize;
        return true;
    }
    ErrorMessage =
        ProgressResult == S_OK
            ? QCoreApplication::translate("FSFolderCopy", "Could not copy file: %1").arg(oldFile)
            : QCoreApplication::translate("FSFolderCopy", "Operation canceled.");
    return false;
#else
    CCopyStateIO state2;
    const bool targetExistedBefore = QFileInfo::exists(newFile);
    state2.StartPos = StartPos;
    state2.QtProgress = [this](quint64 completed) {
        const UInt64 nativeCompleted = completed;
        if (Callback && Callback->SetCompleted(&nativeCompleted) != S_OK)
            return false;
        return !Progress || Progress(completed);
    };
    if (!state2.MyCopyFile(oldFile, newFile, attrib)) {
        if (!targetExistedBefore)
            QFile::remove(newFile);
        ErrorMessage = QCoreApplication::translate("FSFolderCopy", "Operation canceled.");
        return false;
    }
    if (state2.ErrorFileIndex >= 0 || !state2.ErrorMessage.isEmpty()) {
        if (!targetExistedBefore)
            QFile::remove(newFile);
        const QString errorName = state2.ErrorFileIndex == 0 ? oldFile : newFile;
        ErrorMessage = QCoreApplication::translate("FSFolderCopy", "File read or write error: %1")
                           .arg(errorName);
        return false;
    }
    StartPos += state2.CurrentSize;
    return true;
#endif
}

class QtMoveFileProgress final : public NWindows::NFile::NDir::ICopyFileProgress {
  public:
    explicit QtMoveFileProgress(CCopyState &state) : state_(state) {}
    DWORD CopyFileProgress(UInt64 total, UInt64 current) override {
        return state_.ReportSystemProgress(total, current);
    }

  private:
    CCopyState &state_;
};

bool CCopyState::MoveFile_Sys(const QString &oldFile, const QString &newFile,
                              QFileDevice::Permissions attrib) {
    Q_UNUSED(attrib)
    // POSIX MyMoveFile_with_Progress copies EXDEV files; directories must
    // return to CopyFolder's recursive source branch instead.
    if (QFileInfo(oldFile).isDir())
        return QDir().rename(oldFile, newFile);
    const std::wstring oldWide = QDir::toNativeSeparators(oldFile).toStdWString();
    const std::wstring newWide = QDir::toNativeSeparators(newFile).toStdWString();
    FileSize = quint64(QFileInfo(oldFile).size());
    ProgressResult = S_OK;
    QtMoveFileProgress progress(*this);
    const bool moved = NWindows::NFile::NDir::MyMoveFile_with_Progress(
        us2fs(oldWide.c_str()), us2fs(newWide.c_str()), &progress);
    if (ProgressResult != S_OK)
        ErrorMessage = QCoreApplication::translate("FSFolderCopy", "Operation canceled.");
    else if (!moved)
        ErrorMessage =
            QCoreApplication::translate("FSFolderCopy", "Could not move item: %1").arg(oldFile);
    if (moved)
        StartPos += FileSize;
    return moved;
}

static bool ReportError(CCopyState &state, const QString &message) {
    state.ErrorMessage = message;
    if (state.Callback)
        state.Callback->ShowMessage(message.toStdWString().c_str());
    return false;
}

bool CopyFile_Ask(CCopyState &state, const QString &srcPath, const QFileInfo &srcFileInfo,
                  const QString &destPath) {
#ifdef Q_OS_WIN
    const auto caseSensitivity = Qt::CaseInsensitive;
#else
    const auto caseSensitivity = Qt::CaseSensitive;
#endif
    if (QString::compare(destPath, srcPath, caseSensitivity) == 0) {
        state.ErrorMessage =
            QCoreApplication::translate("FSFolderCopy", "Cannot copy file onto itself: %1")
                .arg(destPath);
        return false;
    }

    const bool isLink = QtIsFileSystemLink(srcPath);
    if (!isLink && (!srcFileInfo.exists() || !srcFileInfo.isFile()))
        return ReportError(
            state,
            QCoreApplication::translate("FSFolderCopy", "Source unavailable: %1").arg(srcPath));

    QString destPathNew = destPath;
    if (state.Callback) {
        NWindows::NFile::NFind::CFileInfo nativeInfo;
        const std::wstring wide = QDir::toNativeSeparators(srcPath).toStdWString();
        if (!nativeInfo.Find(us2fs(wide.c_str())))
            return ReportError(
                state,
                QCoreApplication::translate("FSFolderCopy", "Source unavailable: %1").arg(srcPath));
        CMyComBSTR destPathResult;
        Int32 writeAskResult = 0;
        FILETIME sourceTime;
        FiTime_To_FILETIME(nativeInfo.MTime, sourceTime);
        const std::wstring sourceWide = srcPath.toStdWString();
        const std::wstring destWide = destPath.toStdWString();
        if (state.Callback->AskWrite(sourceWide.c_str(), BoolToInt(false), &sourceTime,
                                     &nativeInfo.Size, destWide.c_str(), &destPathResult,
                                     &writeAskResult) != S_OK)
            return ReportError(state,
                               QCoreApplication::translate("FSFolderCopy", "Operation canceled."));
        if (!IntToBool(writeAskResult)) {
            if (state.TotalSize >= nativeInfo.Size) {
                state.TotalSize -= nativeInfo.Size;
                if (state.Callback->SetTotal(state.TotalSize) != S_OK)
                    return ReportError(
                        state, QCoreApplication::translate("FSFolderCopy", "Operation canceled."));
            }
            return state.CallProgress();
        }
        destPathNew = QString::fromWCharArray((LPCOLESTR)destPathResult);
        if (state.Callback->SetCurrentFilePath(sourceWide.c_str()) != S_OK)
            return ReportError(state,
                               QCoreApplication::translate("FSFolderCopy", "Operation canceled."));
    } else if (QFileInfo::exists(destPath) || QFileInfo(destPath).isSymLink()) {
        return ReportError(
            state, QCoreApplication::translate("FSFolderCopy", "Destination unavailable: %1")
                       .arg(destPath));
    }
    if (state.CurrentFile)
        state.CurrentFile(srcPath);
    if (isLink) {
        if (!state.CallProgress())
            return false;
        bool copied = state.MoveMode && QtRenameFileSystemEntry(srcPath, destPathNew);
        if (!copied) {
            if (!QtCopyFileSystemLink(srcPath, destPathNew))
                return ReportError(
                    state, QCoreApplication::translate("FSFolderCopy", "Could not copy link: %1")
                               .arg(srcPath));
            if (!state.CallProgress()) {
                QtRemoveFileSystemEntry(destPathNew);
                return false;
            }
            if (state.MoveMode && !QtRemoveFileSystemEntry(srcPath)) {
                QtRemoveFileSystemEntry(destPathNew);
                return ReportError(
                    state, QCoreApplication::translate("FSFolderCopy", "Could not move link: %1")
                               .arg(srcPath));
            }
        }
        NWindows::NFile::NFind::CFileInfo info;
        if (info.Find(us2fs(QDir::toNativeSeparators(destPathNew).toStdWString().c_str())))
            state.StartPos += info.Size;
        return state.CallProgress();
    }
    if (state.UseReadWriteMode) {
        NFsFolder::CCopyStateIO state2;
        state2.Progress = state.Callback;
        state2.DeleteSrcFile = state.MoveMode;
        state2.TotalSize = state.TotalSize;
        state2.StartPos = state.StartPos;
        const FString from = us2fs(QDir::toNativeSeparators(srcPath).toStdWString().c_str());
        const FString to = us2fs(QDir::toNativeSeparators(destPathNew).toStdWString().c_str());
        NWindows::NFile::NFind::CFileInfo sourceInfo;
        if (!sourceInfo.Find(from))
            return ReportError(
                state,
                QCoreApplication::translate("FSFolderCopy", "Source unavailable: %1").arg(srcPath));
        state.ProgressResult = state2.MyCopyFile(
            from, to, state.IsAltStreamsDest ? DWORD(-1) : sourceInfo.GetWinAttrib());
        if (state.ProgressResult != S_OK)
            return false;
        if (state2.ErrorFileIndex >= 0) {
            if (state2.ErrorMessage.isEmpty())
                state2.ErrorMessage = QString::fromWCharArray(GetLastErrorMessage());
            const FString errorName = state2.ErrorFileIndex == 0 ? from : to;
            state.ProgressResult = SendMessageError(
                state.Callback, UString(state2.ErrorMessage.toStdWString().c_str()), errorName);
            return false;
        }
        state.StartPos += state2.CurrentSize;
        return state.CallProgress();
    }
    const bool copied = state.MoveMode
                            ? state.MoveFile_Sys(srcPath, destPathNew, srcFileInfo.permissions())
                            : state.CopyFile_Sys(srcPath, destPathNew, srcFileInfo.permissions());
    if (!copied) {
        if (state.ErrorMessage.isEmpty())
            state.ErrorMessage =
                QCoreApplication::translate("FSFolderCopy", "Could not copy or move link: %1")
                    .arg(srcPath);
        return ReportError(state, state.ErrorMessage);
    }
    return state.CallProgress();
}

static QString CombinePath(const QString &folderPath, const QString &fileName) {
    QString s(folderPath);
    if (!s.endsWith('/'))
        s += '/';
    s += fileName;
    return s;
}

bool CopyFolder(CCopyState &state, const QString &srcPath, const QString &destPath) {
    if (!state.CallProgress())
        return false;
    if (IsDestChild(srcPath, destPath)) {
        state.ErrorMessage =
            QCoreApplication::translate("FSFolderCopy", "Cannot copy folder onto itself: %1")
                .arg(destPath);
        return false;
    }
    const QFileInfo source(srcPath);
    if (!source.exists() || !source.isDir() || source.isSymLink() || !source.isReadable())
        return ReportError(
            state,
            QCoreApplication::translate("FSFolderCopy", "Source unavailable: %1").arg(srcPath));
    NWindows::NFile::NFind::CFileInfo metadata;
    const FString nativeSource = us2fs(QDir::toNativeSeparators(srcPath).toStdWString().c_str());
    const FString nativeDestination =
        us2fs(QDir::toNativeSeparators(destPath).toStdWString().c_str());
    if (!metadata.Find(nativeSource) || metadata.IsOsSymLink())
        return ReportError(
            state,
            QCoreApplication::translate("FSFolderCopy", "Source unavailable: %1").arg(srcPath));
    const QFileInfo destination(destPath);
    const bool destinationExisted = destination.exists();
    if (state.MoveMode && state.MoveFile_Sys(srcPath, destPath, source.permissions()))
        return true;
    if (QtIsFileSystemLink(destPath))
        return ReportError(state, QCoreApplication::translate("qtfiletransfertransaction",
                                                              "Destination is a symbolic link: %1")
                                      .arg(destPath));
    if (destinationExisted && !destination.isDir())
        return ReportError(
            state, QCoreApplication::translate("CopyRedesign", "Not a folder: %1").arg(destPath));
    if (!(state.Callback ? QDir().mkpath(destPath) : QDir().mkdir(destPath)))
        return ReportError(
            state,
            QCoreApplication::translate("FSFolderCopy", "Cannot create folder: %1").arg(destPath));

    NWindows::NFile::NFind::CEnumerator enumerator;
    const std::wstring sourceWide = QDir::toNativeSeparators(srcPath).toStdWString();
    FString prefix = us2fs(sourceWide.c_str());
    prefix.Add_PathSepar();
    enumerator.SetDirPrefix(prefix);
    for (;;) {
        NWindows::NFile::NFind::CFileInfo fi;
        bool found = false;
#ifdef _WIN32
        if (!enumerator.Next(fi, found))
            return ReportError(
                state, QCoreApplication::translate("FSFolderCopy", "Could not read folder: %1")
                           .arg(srcPath));
#else
        NWindows::NFile::NFind::CDirEntry entry;
        if (!enumerator.Next(entry, found))
            return ReportError(
                state, QCoreApplication::translate("FSFolderCopy", "Could not read folder: %1")
                           .arg(srcPath));
        if (found && !enumerator.Fill_FileInfo(entry, fi, false))
            return ReportError(
                state, QCoreApplication::translate("FSFolderCopy", "Could not read folder: %1")
                           .arg(srcPath));
#endif
        if (!found)
            break;
        const QString name = QString::fromWCharArray(fs2us(fi.Name));
        const QString srcPath2 = CombinePath(srcPath, name);
        const QString destPath2 = CombinePath(destPath, name);
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            if (!CopyFolder(state, srcPath2, destPath2))
                return false;
        } else {
            if (!CopyFile_Ask(state, srcPath2, QFileInfo(srcPath2), destPath2))
                return false;
        }
    }
    // Native services preserve the folder's metadata after child creation has
    // finished changing its timestamps. POSIX ownership/ctime are not writable copies.
    if (!destinationExisted &&
        (!NWindows::NFile::NDir::SetDirTime(nativeDestination, &metadata.CTime, &metadata.ATime,
                                            &metadata.MTime) ||
         !NWindows::NFile::NDir::SetFileAttrib_PosixHighDetect(nativeDestination,
                                                               metadata.GetWinAttrib())))
        return ReportError(state, QCoreApplication::translate(
                                      "FSFolderCopy", "Could not preserve folder metadata: %1")
                                      .arg(destPath));
    if (state.MoveMode && !QDir().rmdir(srcPath))
        return ReportError(
            state,
            QCoreApplication::translate("FSFolderCopy", "Cannot remove folder: %1").arg(srcPath));
    return true;
}

Z7_COM7F_IMF(CFSFolder::CopyTo(Int32 moveMode, const UInt32 *indices, UInt32 numItems,
                               Int32 /* includeAltStreams */, Int32 /* replaceAltStreamColon */,
                               const wchar_t *path, IFolderOperationsExtractCallback *callback)) {
    if (numItems == 0)
        return S_OK;
    if (!indices || !path || !callback)
        return E_INVALIDARG;

    const FString destPath = us2fs(path);
    if (destPath.IsEmpty())
        return E_INVALIDARG;
    bool isAltDest = false;
#ifdef Q_OS_WIN
    isAltDest = NWindows::NFile::NName::IsAltPathPrefix(destPath);
#endif
    const bool isDirectPath = !isAltDest && !IsPathSepar(destPath.Back());
    if (isDirectPath && numItems > 1)
        return E_INVALIDARG;

    CFsFolderStat stat(FString(), callback);
    for (UInt32 i = 0; i < numItems; i++) {
        const UInt32 index = indices[i];
        if (index >= Files.Size())
            return E_INVALIDARG;
        const CDirItem &fi = Files[index];
        const FString srcPath = _path + GetRelPath(fi);
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            if (!isAltDest) {
                stat.Path = srcPath;
                RINOK(stat.Enumerate())
            }
            stat.NumFolders++;
        } else {
            stat.NumFiles++;
            stat.Size += fi.Size;
        }
    }
    RINOK(callback->SetTotal(stat.Size))
    RINOK(callback->SetNumFiles(stat.NumFiles))
    UInt64 completedSize = 0;
    RINOK(callback->SetCompleted(&completedSize))

    CCopyState state;
    state.TotalSize = stat.Size;
    state.Callback = callback;
    state.MoveMode = IntToBool(moveMode);
    state.IsAltStreamsDest = isAltDest;
    state.UseReadWriteMode = isAltDest;
    for (UInt32 i = 0; i < numItems; i++) {
        const CDirItem &fi = Files[indices[i]];
        FString destPath2 = destPath;
        if (!isDirectPath)
            destPath2 += fi.Name;
        const FString srcPath = _path + GetRelPath(fi);
        const QString source = QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(srcPath)));
        const QString target =
            QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(destPath2)));
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            if (isAltDest) {
                RINOK(SendMessageError(callback, k_CannotCopyDirToAltStream, srcPath))
            } else if (!CopyFolder(state, source, target))
                return E_ABORT;
        } else if (!CopyFile_Ask(state, source, QFileInfo(source), target)) {
            return state.ProgressResult != S_OK ? state.ProgressResult : E_ABORT;
        }
    }
    return S_OK;
}

HRESULT CopyFileSystemItems(const UStringVector &itemsPaths, const FString &destDirPrefix,
                            bool moveMode, IFolderOperationsExtractCallback *callback) {
    if (itemsPaths.IsEmpty())
        return S_OK;
    if (destDirPrefix.IsEmpty() || !callback)
        return E_INVALIDARG;
    bool isAltDest = false;
#ifdef Q_OS_WIN
    isAltDest = NWindows::NFile::NName::IsAltPathPrefix(destDirPrefix);
#endif
    if (!isAltDest && !IsPathSepar(destDirPrefix.Back()))
        return E_INVALIDARG;

    CFsFolderStat stat(FString(), callback);
    FOR_VECTOR(i, itemsPaths) {
        const FString path = us2fs(itemsPaths[i]);
        NWindows::NFile::NFind::CFileInfo fi;
        if (!fi.Find(path))
            continue;
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            if (!isAltDest) {
                stat.Path = path;
                RINOK(stat.Enumerate())
            }
            stat.NumFolders++;
        } else {
            stat.NumFiles++;
            stat.Size += fi.Size;
        }
    }
    RINOK(callback->SetTotal(stat.Size))
    UInt64 completedSize = 0;
    RINOK(callback->SetCompleted(&completedSize))

    CCopyState state;
    state.TotalSize = stat.Size;
    state.Callback = callback;
    state.MoveMode = moveMode;
    state.IsAltStreamsDest = isAltDest;
    state.UseReadWriteMode = isAltDest;
    FOR_VECTOR(i, itemsPaths) {
        const FString path = us2fs(itemsPaths[i]);
        NWindows::NFile::NFind::CFileInfo fi;
        if (!fi.Find(path)) {
            const UString error = QtDiagnosticText(QT_TRANSLATE_NOOP("SevenZipDiagnostics",
                                                                     "Cannot find the file : ")) +
                                  itemsPaths[i];
            RINOK(callback->ShowMessage(error))
            continue;
        }
        FString destPath = destDirPrefix;
        destPath += fi.Name;
        const QString source = QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(path)));
        const QString target = QDir::fromNativeSeparators(QString::fromWCharArray(fs2us(destPath)));
        if (fi.IsDir() && !fi.IsOsSymLink()) {
            if (isAltDest) {
                RINOK(SendMessageError(callback, k_CannotCopyDirToAltStream, path))
            } else if (!CopyFolder(state, source, target))
                return E_ABORT;
        } else if (!CopyFile_Ask(state, source, QFileInfo(source), target)) {
            return state.ProgressResult != S_OK ? state.ProgressResult : E_ABORT;
        }
    }
    return S_OK;
}
} // namespace NFsFolder
