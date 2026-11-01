#include "filemanager/providers/FSFolder.h"
#include "support/native_link_fixture.h"
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class Progress final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
  public:
    bool cancel = false;
    UInt64 total = 0;
    QList<UInt64> completed;
    Z7_COM7F_IMF(SetTotal(UInt64 value)) override {
        total = value;
        return cancel ? E_ABORT : S_OK;
    }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *value)) override {
        if (value)
            completed.append(*value);
        return cancel ? E_ABORT : S_OK;
    }
};
} // namespace

class NativeFsOperationsTests : public QObject {
    Q_OBJECT
  private slots:
    void recursiveRemovalPreservesLinkedTargets_data() {
        QTest::addColumn<bool>("native");
        QTest::addColumn<bool>("links");
        QTest::newRow("native tree") << true << false;
        QTest::newRow("Qt tree") << false << false;
        QTest::newRow("native linked tree") << true << true;
        QTest::newRow("Qt linked tree") << false << true;
    }
    void recursiveRemovalPreservesLinkedTargets() {
        QFETCH(bool, native);
        QFETCH(bool, links);
        QTemporaryDir fixture, outside;
        QVERIFY(fixture.isValid() && outside.isValid());
        const QString root = fixture.filePath(QStringLiteral("удалить 空"));
        QVERIFY(QDir().mkpath(root + "/nested/.hidden"));
        const QString sentinel = outside.filePath("keep.txt");
        for (const auto &path : {sentinel, root + "/nested/.hidden/data.txt", root + "/file.txt"}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected"), qint64(9));
        }
        if (links) {
            if (!CreateNativeSymbolicLink(root + "/outside", outside.path()))
                QSKIP("Directory symbolic links are unavailable for this test account");
            QVERIFY(CreateNativeSymbolicLink(root + "/cycle", root));
            QVERIFY(CreateNativeSymbolicLink(root + "/broken", "missing-target"));
        }
        const auto remove = [&](const QString &path) {
            return native ? NWindows::NFile::NDir::RemoveDirWithSubItems(NativeFixturePath(path))
                          : NWindows::NFile::NDir::RemoveDirWithSubItems(path);
        };
        QVERIFY(remove(root));
        QVERIFY(!QFileInfo::exists(root));
        if (links) {
            const QString link = fixture.filePath("directory-link");
            QVERIFY(CreateNativeSymbolicLink(link, outside.path()));
            QVERIFY(remove(link));
            QVERIFY(!QFileInfo::exists(link) && !QFileInfo(link).isSymLink());
            QVERIFY(CreateNativeSymbolicLink(link, outside.path()));
            QVERIFY(remove(link + QDir::separator()));
            QVERIFY(!QFileInfo::exists(link) && !QFileInfo(link).isSymLink());
        }
        QFile remaining(sentinel);
        QVERIFY(remaining.open(QIODevice::ReadOnly));
        QCOMPARE(remaining.readAll(), QByteArray("protected"));
    }
    void caseOnlyRename_data() {
        QTest::addColumn<bool>("directory");
        QTest::newRow("file") << false;
        QTest::newRow("directory") << true;
    }
    void caseOnlyRename() {
        QFETCH(bool, directory);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString oldName = QStringLiteral("MiXeD файл");
        const QString newName = QStringLiteral("mixed файл");
        if (directory)
            QVERIFY(QDir(temp.path()).mkdir(oldName));
        const QString payload = directory ? oldName + QStringLiteral("/keep.txt") : oldName;
        QFile file(temp.filePath(payload));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("protected"), qint64(9));
        file.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        QCOMPARE(operations->Rename(0, newName.toStdWString().c_str(), nullptr), S_OK);
        QCOMPARE(QDir(temp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot),
                 QStringList{newName});
        QFile result(temp.filePath(directory ? newName + QStringLiteral("/keep.txt") : newName));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("protected"));
    }
#ifndef Q_OS_WIN
    void readOnlyDirectoryRejectsFilesystemMutations() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QFile original(temp.filePath("original.txt"));
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("protected"), qint64(9));
        original.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        const QFileDevice::Permissions permissions = QFile::permissions(temp.path());
        const auto restore = qScopeGuard([&] { QFile::setPermissions(temp.path(), permissions); });
        QVERIFY(QFile::setPermissions(temp.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        if (QFileInfo(temp.path()).isWritable())
            QSKIP("This test user can write to a read-only directory");
        QVERIFY(operations->CreateFile(L"new.txt", nullptr) != S_OK);
        QVERIFY(operations->Rename(0, L"renamed.txt", nullptr) != S_OK);
        CMyComPtr<IProgress> progress = new Progress;
        const UInt32 index = 0;
        QVERIFY(operations->Delete(&index, 1, progress) != S_OK);
        QVERIFY(QFileInfo::exists(original.fileName()));
        QVERIFY(!QFileInfo::exists(temp.filePath("new.txt")));
    }
#endif
    void recursiveCancellationReturnsAbortAndPreservesPendingFiles() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("папка внутри"))));
        const QString path = temp.filePath(QStringLiteral("папка внутри/не удалять.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        auto *native = new NFsFolder::CFSFolder(temp.path());
        CMyComPtr<IFolderFolder> folder = native;
        native->CheckBreak = [path](const QString &current) {
            return current.normalized(QString::NormalizationForm_C) !=
                   path.normalized(QString::NormalizationForm_C);
        };
        QCOMPARE(folder->LoadItems(), S_OK);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        CMyComPtr<IProgress> progress = new Progress;
        const UInt32 index = 0;
        QCOMPARE(operations->Delete(&index, 1, progress), E_ABORT);
        QVERIFY(QFileInfo::exists(path));
    }
    void actualInterfaceCreatesRenamesAndDeletesWithoutReplacing() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        const wchar_t *name = L"новый файл.txt";
        const QString path = temp.filePath(QString::fromWCharArray(name));
        QCOMPARE(operations->CreateFile(name, nullptr), S_OK);
        QVERIFY(QFileInfo::exists(path));
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("protected"), qint64(9));
        original.close();
        QVERIFY(operations->CreateFile(name, nullptr) != S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        QCOMPARE(operations->Rename(0, L"renamed файл.txt", nullptr), S_OK);
        const QString renamed = temp.filePath(QStringLiteral("renamed файл.txt"));
        QFile result(renamed);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("protected"));
        result.close();
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(folder->LoadItems(), S_OK);
        QCOMPARE(operations->CreateFile(L"existing", nullptr), S_OK);
        QVERIFY(operations->Rename(0, L"existing", nullptr) != S_OK);
        QVERIFY(QFileInfo::exists(renamed));
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 indices[] = {0, 1};
        auto *progress = new Progress;
        CMyComPtr<IProgress> owner = progress;
        QCOMPARE(operations->Delete(indices, 2, owner), S_OK);
        QCOMPARE(progress->total, UInt64(2));
        QVERIFY(!progress->completed.isEmpty());
        UInt64 previous = 0;
        for (UInt64 value : progress->completed) {
            QVERIFY(value >= previous && value <= progress->total);
            previous = value;
        }
        QVERIFY(QDir(temp.path()).entryList(QDir::Files).isEmpty());
    }
    void directoryFailureAndCancellationPreserveFiles() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        QCOMPARE(operations->CreateFolder(L"папка/child", nullptr), S_OK);
        QVERIFY(QDir(temp.filePath(QStringLiteral("папка/child"))).exists());
        QVERIFY(operations->CreateFolder(L"папка/child", nullptr) != S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        const UInt32 index = 0;
        auto *progress = new Progress;
        CMyComPtr<IProgress> owner = progress;
        progress->cancel = true;
        QCOMPARE(operations->Delete(&index, 1, owner), E_ABORT);
        QVERIFY(QDir(temp.filePath(QStringLiteral("папка/child"))).exists());
        QCOMPARE(operations->Delete(&index, 1, nullptr), E_INVALIDARG);
        const UInt32 invalid = 99;
        progress->cancel = false;
        QCOMPARE(operations->Delete(&invalid, 1, owner), E_INVALIDARG);
        QCOMPARE(operations->Delete(&index, 1, owner), S_OK);
        QVERIFY(!QFileInfo::exists(temp.filePath(QStringLiteral("папка"))));
        QCOMPARE(operations->CopyFromFile(0, L"unused", owner), E_NOTIMPL);
    }
};
QTEST_GUILESS_MAIN(NativeFsOperationsTests)
#include "native_fs_operations_tests.moc"
