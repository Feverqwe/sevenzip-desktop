#include "Common/StringConvert.h"
#include "Windows/ErrorMsg.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivehashoperation.h"
#include "gui/HashGUI.h"
#include "localization/qtlanguageservices.h"
#include <QDir>
#include <QFileInfo>
#include <QPointer>

void ArchiveBackend::hashMethods(const QStringList &paths, const QStringList &methods) {
    if (busy()) {
        emit finished(false, QCoreApplication::translate("archivebackend_hash",
                                                         "Another operation is still running."));
        return;
    }
    lastResult_ = E_INVALIDARG;
    if (paths.isEmpty() || methods.isEmpty()) {
        emit finished(false, QCoreApplication::translate("archivebackend_hash",
                                                         "No files or hash method selected."));
        return;
    }
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists() || info.isSymLink()) {
            emit finished(false, QCoreApplication::translate("archivebackend_hash",
                                                             "File is missing or is a link: %1")
                                     .arg(path));
            return;
        }
    }
    const auto catalog = codecCatalog();
    CExternalCodecs providers;
    lastResult_ = loadHashProviders(providers);
    if (lastResult_ == S_OK) {
        UStringVector selected;
        for (const QString &method : methods)
            selected.Add(method.toStdWString().c_str());
        CHashBundle validation;
        lastResult_ = validation.SetMethods(&providers, selected);
    }
    if (lastResult_ != S_OK) {
        emit finished(
            false, QCoreApplication::translate("archivebackend_hash", "Unsupported hash method."));
        return;
    }
    hashOperation_ = std::make_unique<QtArchiveHashOperation>(paths, methods, catalog);
    const QPointer<QtArchiveHashOperation> operation(hashOperation_.get());
    const quint64 progressGeneration = beginWorkerProgress();
    hashOperation_->Progress = [this, progressGeneration](quint64 completed, quint64 total,
                                                          const QString &path) {
        const int percent = total ? int(qMin(100.0, 100.0 * completed / total)) : 0;
        postWorkerProgress(progressGeneration, percent, path);
    };
    connect(hashOperation_.get(), &QThread::finished, this, [this, operation] {
        if (operation && hashOperation_.get() == operation.data())
            finishHashOperation();
    });
    cancelled_ = false;
    job_ = Job::Hash;
    startWorkerPhase(*hashOperation_);
}

void ArchiveBackend::finishHashOperation() {
    if (!hashOperation_)
        return;
    hashOperation_->wait();
    finishWorkerProgress();
    auto operation = std::move(hashOperation_);
    lastResult_ = operation->Result;
    if (cancelled_)
        lastResult_ = E_ABORT;
    const HRESULT result = lastResult_;
    job_ = Job::None;
    const bool ok = result == S_OK && operation->Bundle.NumErrors == 0;
    const QPointer<ArchiveBackend> owner(this);
    if (result != E_ABORT) {
        for (const QString &line : operation->Output) {
            emit outputReceived(line);
            if (!owner)
                return;
        }
        for (const QString &error : operation->Errors) {
            emit outputReceived(error);
            if (!owner)
                return;
        }
        if (result == S_OK) {
            UString results;
            AddHashBundleRes(results, operation->Bundle);
            emit outputReceived(QString::fromWCharArray(results.Ptr()));
            if (!owner)
                return;
        }
    }
    emit finished(
        ok,
        ok ? QCoreApplication::translate("archivebackend_hash", "Checksums calculated.")
        : result == E_ABORT
            ? QCoreApplication::translate("archivebackend_hash", "Operation canceled.")
            : QCoreApplication::translate("archivebackend_hash", "Checksum calculation failed."));
}

HRESULT ArchiveBackend::validateNativeHashReads(const ArchiveNativeOpen &reader,
                                                const UInt32 *indices, UInt32 count) {
    IInArchive *archive = reader.arc().Archive;
    UInt32 total = 0;
    RINOK(archive->GetNumberOfItems(&total))
    if (count != UInt32(-1) && count && !indices)
        return E_INVALIDARG;
    const QString root =
        QFileInfo(QString::fromWCharArray(reader.arc().Path.Ptr())).dir().canonicalPath();
    if (root.isEmpty())
        return E_INVALIDARG;
    if (count == UInt32(-1)) {
        count = total;
        indices = nullptr;
    }
    for (UInt32 n = 0; n < count; ++n) {
        const UInt32 index = indices ? indices[n] : n;
        if (index >= total)
            return E_INVALIDARG;
        NWindows::NCOM::CPropVariant path;
        RINOK(archive->GetProperty(index, kpidPath, &path))
        if (path.vt != VT_BSTR)
            return E_INVALIDARG;
        QString relative = QString::fromWCharArray(path.bstrVal);
        if (!safePath(relative))
            return E_INVALIDARG;
        relative.replace('\\', '/');
        QString current = root;
        for (const QString &part : relative.split('/')) {
            current = QDir(current).filePath(part);
            NWindows::NFile::NFind::CFileInfo info;
            if (info.Find(us2fs(current.toStdWString().c_str())) && info.IsOsSymLink())
                return E_INVALIDARG;
        }
    }
    return S_OK;
}
