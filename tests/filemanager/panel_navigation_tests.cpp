#include "7zip/Common/StreamObjects.h"
#include "Common/StringConvert.h"
#include "filemanager/OpenCallback.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/qtlocationfolder.h"
#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

namespace {
class CTestFolderManager final : public IFolderManager, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IFolderManager)
  public:
    Z7_IFACE_COM7_IMP(IFolderManager)
    CMyComPtr<IFolderFolder> Folder;
    HRESULT Result = S_OK;
    bool SawWorker = false;
    bool SawStream = false;
};
Z7_COM7F_IMF(CTestFolderManager::OpenFolderFile(IInStream *stream, const wchar_t *, const wchar_t *,
                                                IFolderFolder **folder, IProgress *progress)) {
    *folder = nullptr;
    SawWorker = QThread::currentThread() != qApp->thread();
    SawStream = stream != nullptr;
    CMyComPtr<IProgress> progressOwner = progress;
    CMyComPtr<IArchiveOpenCallback> callback;
    RINOK(progressOwner.QueryInterface(IID_IArchiveOpenCallback, &callback))
    UInt64 files = 1, bytes = 3;
    RINOK(callback->SetTotal(&files, &bytes))
    RINOK(callback->SetCompleted(&files, &bytes))
    if (Result != S_OK)
        return Result;
    CMyComPtr<IFolderFolder> result = Folder;
    *folder = result.Detach();
    return S_OK;
}
Z7_COM7F_IMF(CTestFolderManager::GetExtensions(BSTR *extensions)) {
    *extensions = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(CTestFolderManager::GetIconPath(const wchar_t *, BSTR *path, Int32 *index)) {
    *path = nullptr;
    *index = -1;
    return E_NOTIMPL;
}
bool write(const QString &path, const QByteArray &data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
CTempFileInfo tempInfo(const QString &file, const QString &relative, bool remove = false) {
    CTempFileInfo info;
    info.FileIndex = 0;
    info.FilePath = us2fs(file.toStdWString().c_str());
    info.FolderPath = us2fs(QFileInfo(file).absolutePath().toStdWString().c_str());
    info.RelPath = relative.toStdWString().c_str();
    info.NeedDelete = remove;
    return info;
}
} // namespace

class PanelNavigationTests : public QObject {
    Q_OBJECT
  private slots:
    void bindFilesystemArchiveAndReuse() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString directory = temp.filePath(QStringLiteral("папка й space"));
        QVERIFY(QDir().mkdir(directory));
        const QString archive = temp.filePath("test.zip");
        QVERIFY(write(archive, "abc"));
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(CreateArchiveFolder({{"dir/sub/file.txt", 3, 3}}, UString("zip"), &root),
                 HRESULT(S_OK));
        auto *manager = new CTestFolderManager;
        CMyComPtr<IFolderManager> managerOwner = manager;
        manager->Folder = root;
        CPanel panel;
        panel.AttachFolderManager(manager, QString());
        COpenResult result;
        QCOMPARE(panel.BindToPath(ToUString(directory), UString(), result), HRESULT(S_OK));
        panel.LoadFullPath();
        QVERIFY(panel.IsFSFolder());
        QCOMPARE(QDir::cleanPath(ToQString(panel.GetCurrentFolderPrefix())), directory);
        QCOMPARE(panel.BindToPath(ToUString(archive + "/dir/sub"), UString(), result),
                 HRESULT(S_OK));
        QVERIFY(result.ArchiveIsOpened);
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(1));
        panel.LoadFullPath();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix())),
                 archive + "/dir/sub/");
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetParentDirPrefix())),
                 archive + "/dir/");
        panel.OpenParentFolder();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix())),
                 archive + "/dir/");
        QCOMPARE(panel.BindToPath(ToUString(archive + "/dir"), UString(), result), HRESULT(S_OK));
        QVERIFY(!result.ArchiveIsOpened); // native owner reused, no second archive open
        panel.LoadFullPath();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix())),
                 archive + "/dir/");
        panel.OpenParentFolder();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix())),
                 archive + "/");
        panel.OpenParentFolder();
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(0));
        QVERIFY(panel.IsFSFolder());
        QCOMPARE(QDir::cleanPath(ToQString(panel.GetCurrentFolderPrefix())), temp.path());
        QCOMPARE(panel.BindToPathAndRefresh(ToUString(directory)), HRESULT(S_OK));
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(0));
        panel.LoadFullPath();
        QVERIFY(panel.IsFSFolder());
        QCOMPARE(QDir::cleanPath(ToQString(panel.GetCurrentFolderPrefix())), directory);
        panel.OpenRootFolder();
        panel.LoadFullPath();
        QVERIFY(panel.IsRootFolder());
        QVERIFY(panel.GetCurrentFolderPrefix().IsEmpty());
        QVERIFY(panel.ShouldProcessNotifications());
        panel.CloseOpenFolders();
    }

    void sourcePushPopAndFailedOpen() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString name = QStringLiteral("архив й space.zip");
        const QString file = temp.filePath(name);
        QVERIFY(write(file, "abc"));
        CMyComPtr<IFolderFolder> disk, root;
        QCOMPARE(CreateFilesystemFolder(temp.path(), &disk), HRESULT(S_OK));
        QCOMPARE(CreateArchiveFolder({{"inner.zip", 3, 3}}, UString("zip"), &root), HRESULT(S_OK));
        auto *manager = new CTestFolderManager;
        CMyComPtr<IFolderManager> managerOwner = manager;
        manager->Folder = root;
        CPanel panel(disk);
        panel.AttachFolderManager(manager, temp.filePath("settings.ini"));
        panel.LoadFullPath();
        const UString diskPrefix = panel.GetCurrentFolderPrefix();
        const CTempFileInfo info = tempInfo(file, name);
        COpenResult result;
        manager->Result = E_ABORT;
        QCOMPARE(panel.OpenAsArc(nullptr, info, ToUString(file), UString(), result),
                 HRESULT(E_ABORT));
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(0));
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)disk);
        manager->Result = S_OK;
        QCOMPARE(panel.OpenAsArc(nullptr, info, ToUString(file), UString(), result), HRESULT(S_OK));
        QVERIFY(manager->SawWorker);
        QVERIFY(!manager->SawStream);
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(1));
        QCOMPARE(panel.GetParentFolders().Back().ParentFolderPath, diskPrefix);
        panel.LoadFullPath();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(panel.GetCurrentFolderPrefix())),
                 QDir::fromNativeSeparators(ToQString(diskPrefix) + name + '/'));
        panel.CloseOneLevel();
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(0));
        QVERIFY(!panel.GetFolder());
        QVERIFY(QFileInfo::exists(file));
    }
    void nestedCleanup_data() {
        QTest::addColumn<int>("answer");
        QTest::addColumn<bool>("changed");
        QTest::newRow("unchanged") << int(QMessageBox::Discard) << false;
        QTest::newRow("changed-discard") << int(QMessageBox::Discard) << true;
        QTest::newRow("changed-yes-unsupported-preserves-temp") << int(QMessageBox::Yes) << true;
    }
    void nestedCleanup() {
        QFETCH(int, answer);
        QFETCH(bool, changed);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString outer = temp.filePath("outer.zip");
        const QString nestedDir = temp.filePath("nested");
        QVERIFY(QDir().mkdir(nestedDir));
        const QString nested = nestedDir + "/inner.zip";
        QVERIFY(write(outer, "abc"));
        QVERIFY(write(nested, "abc"));
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(CreateArchiveFolder({{"inner.zip", 3, 3}}, UString("zip"), &root), HRESULT(S_OK));
        auto *manager = new CTestFolderManager;
        CMyComPtr<IFolderManager> managerOwner = manager;
        manager->Folder = root;
        CPanel panel;
        panel.AttachFolderManager(manager, QString());
        COpenResult result;
        QCOMPARE(panel.OpenAsArc(nullptr, tempInfo(outer, "outer.zip"), ToUString(outer), UString(),
                                 result),
                 HRESULT(S_OK));
        QCOMPARE(panel.OpenAsArc(nullptr, tempInfo(nested, "inner.zip", true),
                                 ToUString(outer + "/inner.zip"), UString(), result),
                 HRESULT(S_OK));
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(2));
        QCOMPARE((IFolderFolder *)panel.GetParentFolders().Back().ParentFolder,
                 (IFolderFolder *)root);
        if (changed)
            QVERIFY(write(nested, "modified fixture"));
        int prompts = 0, errors = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            if (box->objectName() == "modifiedArchiveQuestion") {
                ++prompts;
                box->done(answer);
            } else if (box->objectName() == "panelArchiveError") {
                ++errors;
                box->accept();
            }
        });
        responder.start(5);
        panel.CloseOneLevel();
        responder.stop();
        QCOMPARE(prompts, changed ? 1 : 0);
        QCOMPARE(errors, changed && answer == QMessageBox::Yes ? 2 : 0);
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(1));
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)root);
        QCOMPARE(QFileInfo::exists(nested), changed && answer == QMessageBox::Yes);
        QVERIFY(QFileInfo::exists(outer));
        panel.CloseOpenFolders();
        QVERIFY(!panel.GetFolder());
        QCOMPARE(panel.GetParentFolders().Size(), unsigned(0));
    }
    void destructorCleansNestedMemoryDirectory() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString outer = temp.filePath("outer.zip");
        QVERIFY(write(outer, "original"));
        const QString nestedDirectory = temp.filePath(QStringLiteral("native temp й space"));
        QVERIFY(QDir().mkdir(nestedDirectory));
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(CreateArchiveFolder({}, UString("zip"), &root), HRESULT(S_OK));
        auto *manager = new CTestFolderManager;
        CMyComPtr<IFolderManager> managerOwner = manager;
        manager->Folder = root;
        {
            CPanel panel;
            panel.AttachFolderManager(manager, QString());
            COpenResult result;
            QCOMPARE(panel.OpenAsArc(nullptr, tempInfo(outer, "outer.zip"), ToUString(outer),
                                     UString(), result),
                     HRESULT(S_OK));
            auto *buffer = new CBufferInStream;
            CMyComPtr<IInStream> stream = buffer;
            buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>("abc"), 3);
            buffer->Init();
            const QString virtualFile = QDir(nestedDirectory).filePath("virtual.zip");
            QCOMPARE(panel.OpenAsArc(stream, tempInfo(virtualFile, "virtual.zip", true),
                                     ToUString(outer + "/virtual.zip"), UString(), result),
                     HRESULT(S_OK));
            QCOMPARE(panel.GetParentFolders().Size(), unsigned(2));
            QVERIFY(QFileInfo::exists(nestedDirectory));
        }
        QVERIFY(!QFileInfo::exists(nestedDirectory));
        QFile original(outer);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("original"));
    }

    void nativeMemoryLink() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(CreateArchiveFolder({}, UString("zip"), &root), HRESULT(S_OK));
        auto *manager = new CTestFolderManager;
        CMyComPtr<IFolderManager> managerOwner = manager;
        manager->Folder = root;
        CPanel panel;
        panel.AttachFolderManager(manager, QString());
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> stream = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>("abc"), 3);
        buffer->Init();
        COpenResult result;
        const QString nonexistent = temp.filePath("virtual.zip");
        QCOMPARE(panel.OpenAsArc(stream, tempInfo(nonexistent, "virtual.zip"),
                                 ToUString(nonexistent), UString(), result),
                 HRESULT(S_OK));
        QVERIFY(manager->SawStream);
        panel.CloseOpenFolders();
    }
};
QTEST_MAIN(PanelNavigationTests)
#include "panel_navigation_tests.moc"
