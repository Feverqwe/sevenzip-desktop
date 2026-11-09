#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "settings/RegistryUtils.h"
#include <QAction>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <memory>

namespace {
class ProcessControl final {
    const QByteArray previous = qgetenv("SEVENZIP_ASSOC_CONTROL");
    const bool wasSet = qEnvironmentVariableIsSet("SEVENZIP_ASSOC_CONTROL");

  public:
    const QString Path;
    explicit ProcessControl(const QString &path) : Path(path) {
        qputenv("SEVENZIP_ASSOC_CONTROL", path.toUtf8());
    }
    void Release() const { SourceExtractFixture::writeFile(Path + ".exit", "done"); }
    void Finish() const {
        Release();
        QElapsedTimer timeout;
        timeout.start();
        while (QFileInfo::exists(Path + ".ready") && !QFileInfo::exists(Path + ".finished") &&
               timeout.elapsed() < 5000)
            QTest::qWait(10);
    }
    ~ProcessControl() {
        Finish();
        if (wasSet)
            qputenv("SEVENZIP_ASSOC_CONTROL", previous);
        else
            qunsetenv("SEVENZIP_ASSOC_CONTROL");
    }
    QString Opened() const {
        return QString::fromUtf8(SourceExtractFixture::readFile(Path + ".ready"));
    }
};

bool CreateArchive(const QString &directory, const QString &archive, const QString &name) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QStandardPaths::findExecutable("7z"), {"a", "-tzip", "--", archive, name});
    return process.waitForFinished(10000) && process.exitCode() == 0;
}

FileTable *PanelList(MainWindow &window, unsigned pane) {
    auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
    return owner ? dynamic_cast<FileTable *>(owner->findChild<FileTable *>("archiveTable"))
                 : nullptr;
}
int FindRow(const FileTable &list, const QString &name) {
    for (int row = 0; row < list.rowCount(); ++row)
        if (list.model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
} // namespace

class NativeAssociationTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeShellReturnsWaitableProcess() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString program = fixture.filePath(QStringLiteral("процесс 空 with spaces.exe"));
        QtAssociatedFileServices service({});
        IQtProcessHandle *raw = nullptr;
        QString reportedPath;
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                reportedPath = box->text();
                box->accept();
            }
        });
        answer.start(5);
        const HRESULT failure = service.StartApplicationKnown(ToUString(fixture.path()),
                                                              ToUString(program), nullptr, raw);
        answer.stop();
        std::unique_ptr<IQtProcessHandle> failedProcess(raw);
        QCOMPARE(failure, E_FAIL);
        QVERIFY(!failedProcess);
        QVERIFY(reportedPath.contains(program));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_ASSOC_HELPER), program));
        ProcessControl control(fixture.filePath("native shell control"));
        const HRESULT result = service.StartApplicationKnown(ToUString(fixture.path()),
                                                             ToUString(program), nullptr, raw);
        std::unique_ptr<IQtProcessHandle> process(raw);
        QCOMPARE(result, S_OK);
        QTRY_VERIFY(QFileInfo::exists(control.Path + ".ready"));
        QVERIFY(process);
        QCOMPARE(process->Id(), SourceExtractFixture::readFile(control.Path + ".pid").toUInt());
        QCOMPARE(QFileInfo(control.Opened()).canonicalFilePath(),
                 QFileInfo(program).canonicalFilePath());
        QCOMPARE(process->Status(), QtProcessStatus::Running);
        control.Release();
        QTRY_COMPARE(process->Status(), QtProcessStatus::Exited);
        QVERIFY(QFileInfo::exists(control.Path + ".finished"));
    }

    void diskOpenKeepsOriginalAndDoesNotWait() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("процесс 空 with spaces.exe");
        const QString program = fixture.filePath(name);
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_ASSOC_HELPER), program));
        const QByteArray original = SourceExtractFixture::readFile(program);
        ProcessControl control(fixture.filePath("disk shell control"));
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        QtAssociatedFileServices service({});
        CPanel panel;
        panel.AttachFolderManager(manager, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        panel.AttachExternalFileServices(&service);
        COpenResult opened;
        QCOMPARE(panel.BindToPath(ToUString(fixture.path()), UString(), opened), S_OK);
        QCOMPARE(panel.GetFolder()->LoadItems(), S_OK);
        panel.LoadFullPath();
        UInt32 count = 0;
        QCOMPARE(panel.GetFolder()->GetNumberOfItems(&count), S_OK);
        UInt32 index = count;
        for (UInt32 i = 0; i < count; ++i)
            if (ToQString(panel.GetItemName(i)) == name)
                index = i;
        QVERIFY(index < count);
        QCOMPARE(QFileInfo(ToQString(panel.GetItemFullPath(index))).canonicalFilePath(),
                 QFileInfo(program).canonicalFilePath());
        panel.OpenItem(index, false, true);
        QTRY_VERIFY(QFileInfo::exists(control.Path + ".ready"));
        QCOMPARE(QFileInfo(control.Opened()).canonicalFilePath(),
                 QFileInfo(program).canonicalFilePath());
        QVERIFY(!QFileInfo::exists(control.Path + ".finished"));
        control.Release();
        QTRY_VERIFY(QFileInfo::exists(control.Path + ".finished"));
        QCOMPARE(SourceExtractFixture::readFile(program), original);
    }

    void archiveOpenCleansCopyAfterNativeCompletion_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        for (unsigned pane : {0u, 1u})
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(qPrintable(QString("pane%1-%2").arg(pane).arg(language)))
                    << pane << language;
    }
    void archiveOpenCleansCopyAfterNativeCompletion() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("процесс 空 with spaces.exe");
        const QString program = fixture.filePath(name);
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_ASSOC_HELPER), program));
        QVERIFY(CreateArchive(fixture.path(), archive, name));
        const QByteArray programBytes = SourceExtractFixture::readFile(program);
        const QByteArray archiveBytes = SourceExtractFixture::readFile(archive);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CWindowInfo placement{};
        placement.numPanels = 2;
        placement.currentPanel = pane;
        placement.rect.right = 980;
        placement.rect.bottom = 620;
        placement.splitterPos = 490;
        placement.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ProcessControl control(fixture.filePath("archive shell control"));
        // RED may retain the fallback's protected copy. Only this recorded
        // private helper is eligible for cleanup, after its own completion.
        const auto cleanup = qScopeGuard([&] {
            control.Finish();
            if (QFileInfo::exists(control.Path + ".finished") &&
                SourceExtractFixture::readFile(control.Opened()) == programBytes)
                QDir(QFileInfo(control.Opened()).absolutePath()).removeRecursively();
        });
        QStringList unexpectedMessages;
        QTimer modalGuard;
        connect(&modalGuard, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpectedMessages.append(box->objectName() + ": " + box->text());
                qWarning().noquote() << unexpectedMessages.back();
                box->done(QMessageBox::Cancel);
            }
        });
        modalGuard.start(10);
        const QString module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module, 0);
        QCOMPARE(catalog.result(), S_OK);
        ArchiveBackend backend(nullptr, QStandardPaths::findExecutable("7z"));
        QCOMPARE(backend.loadCodecCatalog(module, catalog.version()), S_OK);
        MainWindow window(&backend);
        window.show();
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QVERIFY(twoPanels);
        if (!twoPanels->isChecked())
            twoPanels->trigger();
        auto *list = PanelList(window, pane);
        QVERIFY(list);
        window.activateWindow();
        list->setFocus();
        QApplication::processEvents();
        window.openArchive(archive);
        QVERIFY2(unexpectedMessages.isEmpty(), qPrintable(unexpectedMessages.join('|')));
        QTRY_VERIFY(list->isEnabled() && list->sourcePanel &&
                    list->sourcePanel->GetParentFolders().Size() == 1 && FindRow(*list, name) >= 0);
        const int row = FindRow(*list, name);
        list->setCurrentCell(row, 0);
        list->selectRow(row);
        list->setFocus();
        auto *open = window.findChild<QAction *>("openOutsideAction");
        QVERIFY(open);
        QTRY_VERIFY(open->isEnabled());
        auto *provider = list->sourcePanel->GetFolder();
        open->trigger();
        QVERIFY2(unexpectedMessages.isEmpty(), qPrintable(unexpectedMessages.join('|')));
        QTRY_VERIFY(QFileInfo::exists(control.Path + ".ready"));
        const QString extracted = control.Opened();
        QVERIFY(extracted != program);
        QCOMPARE(SourceExtractFixture::readFile(extracted), programBytes);
        // Original MyThreadFunction treats a process shorter than two seconds
        // as possibly handing off to another process; exercise its long branch.
        QTest::qWait(2200);
        control.Release();
        QTRY_VERIFY(QFileInfo::exists(control.Path + ".finished"));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(extracted), 10000);
        QCOMPARE(list->sourcePanel->GetFolder(), provider);
        QCOMPARE(SourceExtractFixture::readFile(archive), archiveBytes);
        QCOMPARE(SourceExtractFixture::readFile(program), programBytes);
    }
};

QTEST_MAIN(NativeAssociationTests)
#include "native_association_tests.moc"
