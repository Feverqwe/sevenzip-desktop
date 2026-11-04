#pragma once
#include "7zip/IStream.h"
#include "Common/MyCom.h"
#include "mount/archivemountdecision.h"
#include "rep_archivemount_merged.h"
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QStringList>
#include <atomic>
#include <memory>
#include <optional>

class ArchiveReadOnlyView;
class QRemoteObjectHost;

// Actor on a dedicated reader thread. The local QtRO endpoint keeps native
// readers and extraction/password/consent policy out of the SMB process.
class QtArchiveMountSource final : public ArchiveMountSource {
    Q_OBJECT
    const QString user_, password_, share_;
    const QUrl endpoint_;
    QList<MountTreeEntry> entries_;
    std::unique_ptr<QRemoteObjectHost> host_;
    QMutex viewMutex_;
    std::shared_ptr<ArchiveReadOnlyView> view_;
    std::atomic_bool cancelled_{false};
    QHash<quint64, QSet<quint32>> reported_;
    std::optional<bool> extractionAllowed_;
    QString entryPath(quint64 id) const;

  public:
    QtArchiveMountSource(IUnknown *resource, quint64 quota, QString archivePassword, QString user,
                         QString password, QUrl endpoint,
                         QString share = QStringLiteral("Sevenzip"), const QString &folder = {},
                         std::shared_ptr<ArchiveReadOnlyView> preparedView = {});
    ~QtArchiveMountSource() override;
    void requestCancel();
    QList<MountTreeEntry> entries() const override;
    QString user() const override;
    QString password() const override;
    QString share() const override;
    MountTreeRead read(quint64 id, quint64 offset, quint32 length) override;
    MountTreeDirectory listDirectory(quint64 id) override;
    void listening(quint16 port) override;
    void failed(QString error) override;

  public slots:
    void initialize();
    void close();

  signals:
    void prepared(QString limitations, QStringList skipped);
    void closed();
    void serverListening(quint16 port);
    void failure(QString error);
    void entryIssue(quint64 id, quint32 result, QString error, qint32 operation);
    void extracted(quint64 id);
    void extractionRequired(QString path, quint64 size, ArchiveMountDecisionPtr decision);
    void unverifiedDataRequired(QString path, quint64 size, qint32 operation,
                                ArchiveMountDecisionPtr decision);
};
