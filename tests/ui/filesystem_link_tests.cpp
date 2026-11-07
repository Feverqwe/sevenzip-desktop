#include "7zip/PropID.h"
#include "Common/StringConvert.h"
#include "Windows/FileIO.h"
#include "Windows/PropVariant.h"
#include "Windows/TimeUtils.h"
#include "filemanager/providers/FSFolder.h"
#include "fileops/links/qtlinkservices.h"
#include "fileops/native/FileDir.h"
#include "fileops/support/filedeleteprogress.h"
#include "fileops/transfer/filetransferoperation.h"
#ifdef _WIN32
#include "Windows/MemoryLock.h"
#endif
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class FileSystemLinkTests : public QObject {
    Q_OBJECT
    static FString native(const QString &path) {
        return us2fs(QDir::toNativeSeparators(path).toStdWString().c_str());
    }
    static bool create(const QString &path, const QString &target, bool directory) {
#ifdef _WIN32
        NWindows::NSecurity::EnablePrivilege_SymLink();
        CByteBuffer raw;
        NWindows::NFile::FillLinkData(raw, QDir::toNativeSeparators(target).toStdWString().c_str(),
                                      true, false);
        return NWindows::NFile::NIO::SetReparseData(native(path), directory, raw,
                                                    DWORD(raw.Size()));
#else
        Q_UNUSED(directory)
        return NWindows::NFile::NIO::SetSymLink_UString(native(path),
                                                        UString(target.toStdWString().c_str()));
#endif
    }
    static QByteArray raw(const QString &path) {
        CByteBuffer data;
        if (!NWindows::NFile::NIO::GetReparseData(native(path), data))
            return {};
        return QByteArray(reinterpret_cast<const char *>(static_cast<const Byte *>(data)),
                          qsizetype(data.Size()));
    }

  private slots:
    void hardLinkIdentityAndFileMetadata() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath("source");
        const QString hard = temp.filePath("hard");
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("shared"), qint64(6));
        QVERIFY(file.flush());
        const auto modified = QDateTime::fromString("2000-01-01T00:00:00Z", Qt::ISODate);
        QVERIFY(modified.isValid());
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(source).lastModified().toSecsSinceEpoch(), modified.toSecsSinceEpoch());
        QVERIFY(NWindows::NFile::NDir::MyCreateHardLink(native(hard), native(source)));
        NFsFolder::CFSFolder folder(temp.path());
        folder.AddPath(source);
        folder.AddPath(hard);
        NWindows::NCOM::CPropVariant first, second;
        QCOMPARE(folder.GetProperty(0, kpidINode, &first), HRESULT(S_OK));
        QCOMPARE(folder.GetProperty(1, kpidINode, &second), HRESULT(S_OK));
        QCOMPARE(first.vt, VARTYPE(VT_UI8));
        QCOMPARE(second.vt, VARTYPE(VT_UI8));
        QCOMPARE(first.uhVal.QuadPart, second.uhVal.QuadPart);
#ifndef _WIN32
        QVERIFY(QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
#endif
        FileTransferOperation operation({source}, temp.filePath("destination"), false);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY2(result[0][0].toBool(), qPrintable(result[0][1].toString()));
        const QFileInfo copied(temp.filePath("destination/source"));
        QCOMPARE(copied.lastModified().toSecsSinceEpoch(), modified.toSecsSinceEpoch());
#ifndef _WIN32
        QCOMPARE(copied.permissions(), QFileInfo(source).permissions());
#endif
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("shared"));
    }
    void newFolderMetadataSurvivesChildCreation() {
        QTemporaryDir temp;
        const QString source = temp.filePath("tree");
        const QString destination = temp.filePath("copied tree");
        QVERIFY(QDir().mkdir(source));
        QFile input(QDir(source).filePath("child"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("child bytes"), qint64(11));
        input.close();
        NWindows::NFile::NFind::CFileInfo sourceInfo;
        QVERIFY(sourceInfo.Find(native(source)));
        // Give the folder a timestamp distinct from the copy's current time.
        CFiTime modified = sourceInfo.MTime;
#ifdef _WIN32
        const UInt64 ticks = 125911584000000000ULL; // 2000-01-01 UTC FILETIME.
        modified.dwLowDateTime = DWORD(ticks);
        modified.dwHighDateTime = DWORD(ticks >> 32);
        QVERIFY(NWindows::NFile::NDir::SetFileAttrib(native(source), FILE_ATTRIBUTE_HIDDEN));
#else
        modified.tv_sec = 946684800;
        modified.tv_nsec = 0;
        QVERIFY(QFile::setPermissions(source, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                  QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                                  QFileDevice::ExeGroup));
#endif
        QVERIFY(NWindows::NFile::NDir::SetDirTime(native(source), nullptr, nullptr, &modified));
        NFsFolder::CCopyState state;
        QVERIFY2(NFsFolder::CopyFolder(state, source, destination), qPrintable(state.ErrorMessage));
        QCOMPARE(QFileInfo(destination).lastModified().toSecsSinceEpoch(),
                 QFileInfo(source).lastModified().toSecsSinceEpoch());
#ifdef _WIN32
        NWindows::NFile::NFind::CFileInfo copied;
        QVERIFY(copied.Find(native(destination)));
        QVERIFY(copied.IsHidden());
#else
        QCOMPARE(QFileInfo(destination).permissions(), QFileInfo(source).permissions());
#endif
        QFile output(QDir(destination).filePath("child"));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("child bytes"));
    }
    void copyMoveDelete_data() {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("fallback");
        for (const char *kind : {"relative", "absolute", "dangling", "cycle", "directory"})
            for (bool move : {false, true})
                for (bool fallback : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(kind).arg(move).arg(fallback)))
                        << QString(kind) << move << fallback;
#ifdef _WIN32
        for (bool move : {false, true})
            for (bool fallback : {false, true})
                QTest::newRow(qPrintable(QString("junction-%1-%2").arg(move).arg(fallback)))
                    << QString("junction") << move << fallback;
#endif
    }
    void copyMoveDelete() {
        QFETCH(QString, kind);
        QFETCH(bool, move);
        QFETCH(bool, fallback);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        QVERIFY(QDir(temp.path()).mkdir("outside"));
        const QString bytes = temp.filePath("outside/keep");
        QFile file(bytes);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("target bytes"), qint64(12));
        file.close();
        const QString source = temp.filePath(QStringLiteral("ссылка 空"));
        const QString target = kind == "relative"   ? QString("outside/keep")
                               : kind == "cycle"    ? QFileInfo(source).fileName()
                               : kind == "dangling" ? QString("missing target")
                               : (kind == "directory" || kind == "junction")
                                   ? temp.filePath("outside")
                                   : bytes;
#ifdef _WIN32
        if (kind == "junction") {
            CByteBuffer data;
            NWindows::NFile::FillLinkData(
                data, QDir::toNativeSeparators(target).toStdWString().c_str(), false, false);
            QVERIFY(NWindows::NFile::NIO::SetReparseData(native(source), true, data,
                                                         DWORD(data.Size())));
        } else
#endif
            QVERIFY(create(source, target, kind == "directory"));
        const QByteArray original = raw(source);
        QVERIFY(!original.isEmpty());
        NWindows::NFile::NFind::CFileInfo sourceInfo;
        QVERIFY(sourceInfo.Find(native(source)));
        QString destination = temp.filePath("destination");
        std::unique_ptr<QTemporaryDir> crossVolume;
#ifdef Q_OS_LINUX
        if (fallback) {
            crossVolume = std::make_unique<QTemporaryDir>("/dev/shm/sevenzip-link-XXXXXX");
            QVERIFY(crossVolume->isValid());
            destination = crossVolume->path();
            NWindows::NFile::NFind::CFileInfo destinationInfo;
            QVERIFY(destinationInfo.Find(native(destination)));
            QVERIFY(destinationInfo.dev != sourceInfo.dev);
            // Exercise the source primitive as well as the staged Qt consumer.
            const QString direct = temp.filePath("direct source");
            const QString directTarget = QDir(destination).filePath("direct target");
            QVERIFY(QtCopyFileSystemLink(source, direct));
            NFsFolder::CCopyState state;
            state.MoveMode = move;
            QVERIFY2(NFsFolder::CopyFile_Ask(state, direct, QFileInfo(direct), directTarget),
                     qPrintable(state.ErrorMessage));
            QCOMPARE(raw(directTarget), original);
            QCOMPARE(QtIsFileSystemLink(direct), !move);
            QVERIFY(QtRemoveFileSystemEntry(directTarget));
        }
#endif
        FileTransferOperation::FileSystemAdapter adapter;
        if (fallback && !crossVolume)
            adapter.trySourceRename = [](const QString &, const QString &) { return false; };
        FileTransferOperation operation({source}, destination, move, nullptr, adapter);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY2(result[0][0].toBool(), qPrintable(result[0][1].toString()));
        const QString copied = QDir(destination).filePath(QFileInfo(source).fileName());
        QCOMPARE(raw(copied), original);
        QCOMPARE(QtIsFileSystemLink(source), !move);
        NWindows::NFile::NFind::CFileInfo copiedInfo;
        QVERIFY(copiedInfo.Find(native(copied)));
        FILETIME before, after;
        FiTime_To_FILETIME(sourceInfo.MTime, before);
        FiTime_To_FILETIME(copiedInfo.MTime, after);
        QCOMPARE(after.dwLowDateTime, before.dwLowDateTime);
        QCOMPARE(after.dwHighDateTime, before.dwHighDateTime);
        auto *folderSpec = new NFsFolder::CFSFolder(destination);
        CMyComPtr<IFolderFolder> lifetime = folderSpec;
        auto &folder = *folderSpec;
        QCOMPARE(folder.LoadItems(), HRESULT(S_OK));
        QCOMPARE(folder.Files.Size(), unsigned(1));
        CMyComPtr<IArchiveGetRawProps> properties;
        QCOMPARE(lifetime.QueryInterface(IID_IArchiveGetRawProps, &properties), HRESULT(S_OK));
        UInt32 count = 99;
        QCOMPARE(properties->GetNumRawProps(&count), HRESULT(S_OK));
#ifdef _WIN32
        QCOMPARE(count, UInt32(1));
        const void *data = nullptr;
        UInt32 size = 0, type = 0;
        QCOMPARE(properties->GetRawProp(0, kpidNtReparse, &data, &size, &type), HRESULT(S_OK));
        QCOMPARE(QByteArray(static_cast<const char *>(data), size), original);
        QCOMPARE(type, UInt32(NPropDataType::kRaw));
#else
        QCOMPARE(count, UInt32(0));
#endif
        const UInt32 index = 0;
        CMyComPtr<IProgress> progress = new FileDeleteProgress;
        QCOMPARE(folder.Delete(&index, 1, progress), S_OK);
        QVERIFY(!QtIsFileSystemLink(copied));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("target bytes"));
    }
    void cancelAndOccupiedDestinationPreserveObjects() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("destination"));
        const QString source = temp.filePath("link");
        QVERIFY(create(source, "missing", false));
        const QByteArray original = raw(source);
        FileTransferOperation operation({source}, temp.filePath("destination"), true);
        QSignalSpy result(&operation, &FileTransferOperation::result);
        operation.cancel();
        operation.start();
        QVERIFY(operation.wait(30000));
        QTRY_COMPARE(result.size(), 1);
        QVERIFY(!result[0][0].toBool());
        QCOMPARE(raw(source), original);
        QVERIFY(create(temp.filePath("destination/link"), "other missing", false));
        const QByteArray occupied = raw(temp.filePath("destination/link"));
        QVERIFY(!QtCopyFileSystemLink(source, temp.filePath("destination/link")));
        QCOMPARE(raw(temp.filePath("destination/link")), occupied);
        QCOMPARE(raw(source), original);
    }
    void permissionFailurePreservesLinkAndTarget() {
        QTemporaryDir temp;
        const QString source = temp.filePath("link");
        QVERIFY(create(source, "old missing", false));
        const QByteArray original = raw(source);
#ifdef _WIN32
        struct Restore {
            ~Restore() { NWindows::NSecurity::EnablePrivilege_SymLink(); }
        } restore;
        QVERIFY(NWindows::NSecurity::EnablePrivilege(SE_RESTORE_NAME, false));
        QVERIFY(NWindows::NSecurity::EnablePrivilege(TEXT("SeCreateSymbolicLinkPrivilege"), false));
        const QString denied = temp.filePath("denied");
        QVERIFY(!QtCopyFileSystemLink(source, denied));
        QVERIFY(!QFileInfo::exists(denied));
#else
        const auto permissions = QFileInfo(temp.path()).permissions();
        struct Restore {
            QString path;
            QFileDevice::Permissions permissions;
            ~Restore() { QFile::setPermissions(path, permissions); }
        } restore{temp.path(), permissions};
        QVERIFY(QFile::setPermissions(temp.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const QString denied = temp.filePath("denied");
        QVERIFY(!QtCopyFileSystemLink(source, denied));
        QVERIFY(!QFileInfo(denied).isSymLink());
#endif
        QCOMPARE(raw(source), original);
    }
};
QTEST_GUILESS_MAIN(FileSystemLinkTests)
#include "filesystem_link_tests.moc"
