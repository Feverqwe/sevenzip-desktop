// Bounded output policy for the source ArchiveExtractCallback path algorithm.
#pragma once
#include "Common/StringConvert.h"
#include "archive/backend/archivebackend.h"
#include "archive/callbacks/ArchiveExtractCallback.h"
#include "platform/temp/qtownedtempidentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

class CQtExtractPathPolicy final : public IQtExtractPathPolicy {
    QString _destination;
    QSet<QString> _inputIdentities;

  public:
    explicit CQtExtractPathPolicy(const QString &destination, const QStringList &inputs = {})
        : _destination(destination) {
        for (const auto &input : inputs) {
            const QString identity = QtOwnedTempIdentity(QFileInfo(input).canonicalFilePath());
            if (!identity.isEmpty())
                _inputIdentities.insert(identity);
        }
    }
    HRESULT CheckReplace(const FString &path, UString &message) override {
        if (_inputIdentities.isEmpty())
            return S_OK;
        const QString output = QString::fromWCharArray(fs2us(path).Ptr());
        if (!_inputIdentities.contains(QtOwnedTempIdentity(QFileInfo(output).canonicalFilePath())))
            return S_OK;
        message = QCoreApplication::translate("archivebackend_validation",
                                              "Cannot overwrite an input archive: %1")
                      .arg(output)
                      .toStdWString()
                      .c_str();
        return E_INVALIDARG;
    }
    HRESULT CheckPath(const FString &path, bool directory, bool alternateStream,
                      UString &message) override {
        QString error;
        QString output = QString::fromWCharArray(fs2us(path).Ptr());
        // The source has corrected the stream name and chosen its base file.
        // The validator checks an NTFS stream's owner or the standalone POSIX
        // output, retaining their different filesystem link boundaries.
        if (alternateStream) {
            if (ArchiveBackend::validateExtractionStreamOutput(_destination, output, &error))
                return S_OK;
            message = error.toStdWString().c_str();
            return E_INVALIDARG;
        }
        if (ArchiveBackend::validateExtractionOutput(_destination, output, directory, &error))
            return S_OK;
        message = error.toStdWString().c_str();
        return E_INVALIDARG;
    }
    HRESULT CheckLink(const FString &path, const FString &target, bool relative,
                      UString &message) override {
        // The source has already normalized/rebased the link. Only resolve its
        // final filesystem location here; source link algorithms remain owners.
        const QString from = QString::fromWCharArray(fs2us(path).Ptr());
        const QString to = QString::fromWCharArray(fs2us(target).Ptr());
        const QString resolved = relative ? QDir(QFileInfo(from).absolutePath()).filePath(to) : to;
        QString error;
        if (ArchiveBackend::validateExtractionOutput(_destination, from, false, &error) &&
            ArchiveBackend::validateExtractionOutput(_destination, resolved,
                                                     QFileInfo(resolved).isDir(), &error))
            return S_OK;
        message = error.toStdWString().c_str();
        return E_INVALIDARG;
    }
};
