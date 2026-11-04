#include "filemanager/providers/FSFolder.h"
#include "support/native_link_fixture.h"
#include <QFile>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>

namespace {
bool Write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           file.write(bytes) == bytes.size();
}
QByteArray Read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

class DestinationRace;
thread_local DestinationRace *activeRace = nullptr;

// Scope one competing filesystem entry to the native rename boundary. Both
// native APIs forward to the real kernel operation; no production hook is used.
class DestinationRace {
    DestinationRace *previous_ = activeRace;
    const QString path_;
    const QByteArray encoded_;
    const int kind_;
    bool attempted_ = false;

  public:
    bool Created = false;
    DestinationRace(const QString &path, int kind)
        : path_(path), encoded_(QFile::encodeName(path)), kind_(kind) {
        activeRace = this;
    }
    ~DestinationRace() { activeRace = previous_; }
    void Intervene(const char *destination) {
        if (kind_ < 0 || attempted_ || encoded_ != destination)
            return;
        attempted_ = true;
        Created = kind_ == 0   ? Write(path_, "competing writer bytes")
                  : kind_ == 1 ? QDir().mkdir(path_)
                               : CreateNativeSymbolicLink(path_, "competing missing target");
    }
};
} // namespace

extern "C" int __real_rename(const char *, const char *);
extern "C" int __real_renameat2(int, const char *, int, const char *, unsigned);
extern "C" int __wrap_rename(const char *source, const char *destination) {
    if (activeRace)
        activeRace->Intervene(destination);
    return __real_rename(source, destination);
}
extern "C" int __wrap_renameat2(int sourceDir, const char *source, int destinationDir,
                                const char *destination, unsigned flags) {
    if (activeRace && destinationDir == AT_FDCWD)
        activeRace->Intervene(destination);
    return __real_renameat2(sourceDir, source, destinationDir, destination, flags);
}

class NativeFsRenameTests final : public QObject {
    Q_OBJECT
  private slots:
    void renameWithoutReplacing_data() {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("competing");
        for (bool competing : {false, true}) {
            const QString prefix = competing ? "competing " : "new ";
            QTest::newRow(qPrintable(prefix + "file")) << 0 << competing;
            QTest::newRow(qPrintable(prefix + "directory")) << 1 << competing;
            QTest::newRow(qPrintable(prefix + "symbolic link")) << 2 << competing;
        }
    }
    void renameWithoutReplacing() {
        QFETCH(int, kind);
        QFETCH(bool, competing);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("исходник 空"));
        const QString destination = fixture.filePath(QStringLiteral("новое имя 空"));
        if (kind == 0)
            QVERIFY(Write(source, "source bytes"));
        else if (kind == 1) {
            QVERIFY(QDir().mkdir(source));
            QVERIFY(Write(source + "/child", "source bytes"));
        } else
            QVERIFY(CreateNativeSymbolicLink(source, "source missing target"));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        DestinationRace race(destination, competing ? kind : -1);
        const HRESULT result = operations->Rename(
            0, QFileInfo(destination).fileName().toStdWString().c_str(), nullptr);
        QCOMPARE(race.Created, competing);
        QCOMPARE(result == S_OK, !competing);
        if (!competing) {
            QVERIFY(!QFileInfo::exists(source));
            QVERIFY(!QFileInfo(source).isSymLink());
        }
        if (kind == 0) {
            if (competing)
                QCOMPARE(Read(source), QByteArray("source bytes"));
            QCOMPARE(Read(destination),
                     QByteArray(competing ? "competing writer bytes" : "source bytes"));
        } else if (kind == 1) {
            if (competing)
                QCOMPARE(Read(source + "/child"), QByteArray("source bytes"));
            QVERIFY(QFileInfo(destination).isDir());
            if (competing)
                QVERIFY(
                    QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
            else
                QCOMPARE(Read(destination + "/child"), QByteArray("source bytes"));
        } else {
            if (competing)
                QCOMPARE(QFileInfo(source).symLinkTarget(),
                         fixture.filePath("source missing target"));
            QCOMPARE(
                QFileInfo(destination).symLinkTarget(),
                fixture.filePath(competing ? "competing missing target" : "source missing target"));
        }
    }

    void crossFilesystemFileMove_data() {
        QTest::addColumn<bool>("occupied");
        QTest::newRow("new destination") << false;
        QTest::newRow("occupied destination") << true;
    }
    void crossFilesystemFileMove() {
        QFETCH(bool, occupied);
        if (!QFileInfo("/dev/shm").isDir() ||
            QStorageInfo("/dev/shm").device() == QStorageInfo(QDir::tempPath()).device())
            QSKIP("No separate writable /dev/shm filesystem");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QTemporaryDir other("/dev/shm/sevenzip-rename-XXXXXX");
        if (!other.isValid())
            QSKIP("Separate /dev/shm filesystem is not writable");
        const QString source = fixture.filePath(QStringLiteral("исходник 空"));
        const QString destination = other.filePath(QStringLiteral("назначение 空"));
        QVERIFY(Write(source, "cross filesystem source bytes"));
        if (occupied)
            QVERIFY(Write(destination, "existing destination bytes"));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        const auto name = QDir(fixture.path()).relativeFilePath(destination).toStdWString();
        const HRESULT result = operations->Rename(0, name.c_str(), nullptr);
        if (occupied) {
            QVERIFY(result != S_OK);
            QCOMPARE(Read(source), QByteArray("cross filesystem source bytes"));
            QCOMPARE(Read(destination), QByteArray("existing destination bytes"));
        } else {
            QCOMPARE(result, S_OK);
            QVERIFY(!QFileInfo::exists(source));
            QCOMPARE(Read(destination), QByteArray("cross filesystem source bytes"));
        }
    }
};
QTEST_GUILESS_MAIN(NativeFsRenameTests)
#include "native_fs_rename_tests.moc"
