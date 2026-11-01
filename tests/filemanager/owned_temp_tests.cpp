#include "Common/StringConvert.h"
#include "filemanager/panels/Panel.h"
#include "fileops/native/FileDir.h"
#include "platform/support/native_link_fixture.h"
#include "platform/temp/qtownedtempdir.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#ifdef __linux__
#include <QScopeGuard>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// The stored v1 Linux format predates permission-independent file handles.
QString LegacyDirectoryIdentity(const QString &path) {
    const QByteArray name = QFile::encodeName(path);
    struct stat info;
    if (::lstat(name.constData(), &info) != 0 || !S_ISDIR(info.st_mode))
        return {};
    QString identity = QString("%1:%2:1").arg(qulonglong(info.st_dev)).arg(qulonglong(info.st_ino));
    struct statx extended = {};
    if (::statx(AT_FDCWD, name.constData(), AT_SYMLINK_NOFOLLOW, STATX_INO | STATX_BTIME,
                &extended) == 0 &&
        (extended.stx_mask & STATX_BTIME))
        identity +=
            QString(":%1:%2").arg(extended.stx_btime.tv_sec).arg(extended.stx_btime.tv_nsec);
    const int descriptor = ::open(name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
        return {};
    const auto close = qScopeGuard([descriptor] { ::close(descriptor); });
    long generation = 0;
    if (::ioctl(descriptor, FS_IOC_GETVERSION, &generation) == 0)
        identity += QString(":g%1").arg(qulonglong(static_cast<unsigned long>(generation)));
    return identity;
}
} // namespace
#endif

class OwnedTempTests : public QObject {
    Q_OBJECT
    QByteArray previousRoot;
    QTemporaryDir fixture;
    QString registryPath;
    static bool write(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
    }

  private slots:
    void init() {
        previousRoot = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        registryPath = fixture.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", registryPath.toUtf8());
    }
    void cleanup() { qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", previousRoot); }
#ifdef __linux__
    void previousRegistryPreservesActiveLeaseAndRetainedFiles() {
        const QString path = fixture.filePath(QStringLiteral("old retained 文件"));
        QVERIFY(QDir().mkpath(path));
        const QString sentinel = path + QStringLiteral("/keep.txt");
        QVERIFY(write(sentinel));
        const QString identity = LegacyDirectoryIdentity(path);
        QVERIFY(!identity.isEmpty());
        const QString key =
            QUuid::createUuidV5(QUuid("ae8d385e-4ab9-4bbf-818e-088be7c96ef2"), identity.toUtf8())
                .toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(registryPath));
        const QByteArray record = QJsonDocument(QJsonObject{{"version", 1},
                                                            {"path", path},
                                                            {"identity", identity},
                                                            {"external", false}})
                                      .toJson(QJsonDocument::Compact);
        QFile stored(registryPath + '/' + key + ".json");
        QVERIFY(stored.open(QIODevice::WriteOnly));
        QCOMPARE(stored.write(record), record.size());
        stored.close();
        QLockFile previousOwner(registryPath + '/' + key + ".lock");
        previousOwner.setStaleLockTime(0);
        QVERIFY(previousOwner.tryLock());
        QtOwnedTempRegistry registry;
        auto entries = registry.Entries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().active);
        QVERIFY(!registry.ClaimForDeletion(entries.first()));
        QVERIFY(!registry.RegisterCreated(path));
        previousOwner.unlock();
        QtOwnedTempRegistry restarted(registry.root());
        entries = restarted.Entries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(!entries.first().active);
        auto claim = restarted.ClaimForDeletion(entries.first());
        QVERIFY(claim);
        QVERIFY(!registry.ClaimForDeletion(entries.first()));
        QFile contents(sentinel);
        QVERIFY(contents.open(QIODevice::ReadOnly));
        QCOMPARE(contents.readAll(), QByteArray("fixture"));
        contents.close();
        QVERIFY(QDir(path).removeRecursively());
        claim.reset();
        QVERIFY(restarted.Entries().isEmpty());
    }
#endif
    void activeQtOwnerAndCopiedLeaseProtectUntilLastOwner() {
        QtOwnedTempRegistry registry;
        QString path;
        std::shared_ptr<QtOwnedTempLease> handoff;
        {
            QtOwnedTempDir directory;
            QVERIFY(directory.isValid());
            path = directory.path();
            QVERIFY(write(directory.filePath(QStringLiteral("данные с пробелом.txt"))));
            const auto entries = registry.Entries();
            QCOMPARE(entries.size(), 1);
            QVERIFY(entries.first().active);
            QVERIFY(!registry.ClaimForDeletion(entries.first()));
            QVERIFY(!registry.RegisterCreated(directory.path()));
            handoff = directory.lease();
        }
        // Automatic cleanup has run before the lease dies; no stale delete row.
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(registry.Entries().isEmpty());
        handoff.reset();
        QVERIFY(QDir(registry.root()).entryList({"*.json"}, QDir::Files).isEmpty());
    }
    void nativeDisableDeletingTransfersToCopiedFolderRecord() {
        QtOwnedTempRegistry registry;
        CTempFileInfo nested;
        QString path;
        {
            NWindows::NFile::NDir::CTempDir native;
            QVERIFY(native.Create(FTEXT("7zO")));
            path = QString::fromWCharArray(fs2us(native.GetPath()).Ptr());
            QVERIFY(write(path + QStringLiteral("/вложенный файл.txt")));
            nested.OwnedTempLease = native.OwnedLease();
            native.DisableDeleting();
        }
        auto entries = registry.Entries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().active);
        QVERIFY(!registry.ClaimForDeletion(entries.first()));
        CTempFileInfo another = nested;
        nested.OwnedTempLease.reset();
        QVERIFY(registry.Entries().first().active);
        another.OwnedTempLease.reset();
        QtOwnedTempRegistry restarted(registry.root());
        entries = restarted.Entries();
        QCOMPARE(entries.size(), 1);
        QVERIFY(!entries.first().active);
        auto claim = restarted.ClaimForDeletion(entries.first());
        QVERIFY(claim);
        QVERIFY(!registry.ClaimForDeletion(entries.first()));
        QVERIFY(NWindows::NFile::NDir::RemoveDirWithSubItems(path));
        claim.reset();
        QVERIFY(restarted.Entries().isEmpty());
    }
    void retainedExternalFileRequiresProvenProcessCompletion() {
        QtOwnedTempRegistry registry;
        const QString path = fixture.filePath("external edit.txt");
        QVERIFY(write(path));
        auto owner = registry.RegisterCreated(path);
        QVERIFY(owner && owner->ProtectExternal());
        auto copy = owner;
        owner.reset();
        auto entry = registry.Entries().first();
        QVERIFY(entry.active && entry.externallyProtected);
        QVERIFY(!registry.ClaimForDeletion(entry));
        QVERIFY(copy->ExternalFinished());
        copy.reset();
        entry = QtOwnedTempRegistry(registry.root()).Entries().first();
        QVERIFY(!entry.active && !entry.externallyProtected);
        QVERIFY(registry.ClaimForDeletion(entry));
        // An unconfirmed association remains protected even across restart.
        const QString uncertainPath = fixture.filePath("unconfirmed association.txt");
        QVERIFY(write(uncertainPath));
        auto uncertain = registry.RegisterCreated(uncertainPath);
        QVERIFY(uncertain && uncertain->ProtectExternal());
        uncertain.reset();
        for (const auto &candidate : registry.Entries())
            if (candidate.externallyProtected)
                QVERIFY(!registry.ClaimForDeletion(candidate));
    }
    void deletedRetainedDirectoryAllowsLaterNativeCreation() {
        // Source intentionally keeps a failed edit for recovery. The user can
        // later delete that directory outside the application. A subsequent
        // source Open/Edit must still be able to create its private directory.
        for (unsigned iteration = 0; iteration < 50; ++iteration) {
            QString path;
            {
                NWindows::NFile::NDir::CTempDir native;
                QVERIFY2(native.Create(FTEXT("7zO")), "A deleted retained object blocked creation");
                path = QString::fromWCharArray(fs2us(native.GetPath()).Ptr());
                native.DisableDeleting();
            }
            QVERIFY(QDir(path).removeRecursively());
        }
        QVERIFY(QtOwnedTempRegistry().Entries().isEmpty());
    }
    void unownedNamesAndReplacedIdentityNeverBecomeCandidates() {
        QtOwnedTempRegistry registry;
        const QString user = fixture.filePath("7zO-user-files");
        QVERIFY(QDir().mkpath(user));
        QVERIFY(write(user + "/keep.txt"));
        const QString owned = fixture.filePath(QStringLiteral("каталог с пробелом"));
        QVERIFY(QDir().mkpath(owned));
        auto lease = registry.RegisterCreated(owned);
        QVERIFY(lease);
        const auto entry = registry.Entries().first();
        lease.reset();
        QVERIFY(QDir().rename(owned, owned + "-original"));
        QVERIFY(QDir().mkpath(owned));
        QVERIFY(write(owned + "/replacement.txt"));
        QVERIFY(registry.Entries().isEmpty());
        QVERIFY(!registry.ClaimForDeletion(entry));
        QVERIFY(QFileInfo::exists(user + "/keep.txt"));
        QVERIFY(QFileInfo::exists(owned + "/replacement.txt"));
    }
    void deletedOwnedObjectCannotClaimRecreatedUserDirectory() {
        QtOwnedTempRegistry registry;
        const QString path = fixture.filePath("recreated directory");
        for (unsigned iteration = 0; iteration < 50; ++iteration) {
            QVERIFY(QDir().mkpath(path));
            auto lease = registry.RegisterCreated(path);
            QVERIFY(lease);
            const auto entry = registry.Entries().first();
            lease.reset();
            QVERIFY(QDir(path).removeRecursively());
            QVERIFY(QDir().mkpath(path));
            QVERIFY(write(path + "/user.txt"));
            QVERIFY(registry.Entries().isEmpty());
            QVERIFY(!registry.ClaimForDeletion(entry));
            QFile user(path + "/user.txt");
            QVERIFY(user.open(QIODevice::ReadOnly));
            QCOMPARE(user.readAll(), QByteArray("fixture"));
            user.close();
            QVERIFY(QDir(path).removeRecursively());
        }
    }
    void linkedRootAndLinkedObjectAreRejected() {
        QtOwnedTempRegistry registry;
        const QString target = fixture.filePath("user target");
        QVERIFY(QDir().mkpath(target));
        QVERIFY(write(target + "/keep.txt"));
        const QString link = fixture.filePath("directory link");
        QVERIFY(CreateNativeSymbolicLink(link, target));
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(!registry.RegisterCreated(link));
        QtOwnedTempRegistry linked(link);
        QVERIFY(!linked.RegisterCreated(target));
        QVERIFY(linked.Entries().isEmpty());
        QVERIFY(QFileInfo::exists(target + "/keep.txt"));
    }
    void nativeFileIsActiveThroughWriteAndPublication() {
        QtOwnedTempRegistry registry;
        const auto prefix = fixture.filePath("archive.7z").toStdWString();
        NWindows::NFile::NIO::COutFile output;
        QString temporary;
        {
            NWindows::NFile::NDir::CTempFile native;
            QVERIFY(native.Create(us2fs(prefix.c_str()), &output));
            temporary = QString::fromWCharArray(fs2us(native.GetPath()).Ptr());
            const auto entry = registry.Entries().first();
            QVERIFY(entry.active);
            QVERIFY(!registry.ClaimForDeletion(entry));
            QVERIFY(output.Close());
            QVERIFY(native.MoveTo(us2fs(prefix.c_str()), false, nullptr));
        }
        QVERIFY(QFileInfo::exists(QString::fromStdWString(prefix)));
        QVERIFY(!QFileInfo::exists(temporary));
        QVERIFY(registry.Entries().isEmpty());
    }
    void failedRegistryDoesNotLeaveUnregisteredNativeOrQtObjects() {
        const QString invalidRoot = fixture.filePath("not-a-registry-directory.txt");
        QVERIFY(write(invalidRoot));
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", invalidRoot.toUtf8());
        QtOwnedTempDir qt;
        QVERIFY(!qt.isValid());
        QVERIFY(!QFileInfo::exists(qt.path()));
        NWindows::NFile::NDir::CTempDir native;
        QVERIFY(!native.Create(FTEXT("7zO")));
        QVERIFY(!QFileInfo::exists(QString::fromWCharArray(fs2us(native.GetPath()).Ptr())));
        const auto prefix = fixture.filePath("unregistered.7z").toStdWString();
        NWindows::NFile::NIO::COutFile output;
        NWindows::NFile::NDir::CTempFile file;
        QVERIFY(!file.Create(us2fs(prefix.c_str()), &output));
        QVERIFY(!QFileInfo::exists(QString::fromWCharArray(fs2us(file.GetPath()).Ptr())));
        QVERIFY(QFileInfo::exists(invalidRoot));
    }
};
QTEST_GUILESS_MAIN(OwnedTempTests)
#include "owned_temp_tests.moc"
