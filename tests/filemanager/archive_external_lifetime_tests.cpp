#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "filemanager/panels/Panel.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempregistry.h"
#include "settings/RegistryUtils.h"
#include "support/widget_dialogs.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <atomic>
#include <new>

using namespace ExternalEditorFixture;

namespace {
class CErrorHandle final : public IQtProcessHandle {
    std::atomic_bool &_closed;
    bool _throwName;
    bool _exited;

  public:
    explicit CErrorHandle(std::atomic_bool &closed, bool throwName = false, bool exited = false)
        : _closed(closed), _throwName(throwName), _exited(exited) {}
    ~CErrorHandle() override { _closed.store(true); }
    UInt32 Id() const override { return 100; }
    UString FileName() const override {
        if (_throwName)
            throw std::bad_alloc();
        return UString(L"owned-fixture");
    }
    QtProcessStatus Status() override {
        return _exited ? QtProcessStatus::Exited : QtProcessStatus::Error;
    }
};
class CThrowingSnapshotServices final : public IQtProcessServices {
  public:
    void GetSnapshot(CObjectVector<CSnapshotProcess> &) override { throw std::bad_alloc(); }
    IQtProcessHandle *OpenProcess(UInt32) override { return nullptr; }
    UInt32 CurrentProcessId() const override { return 0; }
};
class CProcessService final : public IQtExternalFileServices {
    QtAssociatedFileServices _native{std::function<bool(const QUrl &)>()};
    CThrowingSnapshotServices _snapshot;

  public:
    bool FailWait = false;
    bool FailName = false;
    bool FailSnapshot = false;
    bool FailAfterLaunch = false;
    HRESULT LaunchResult = S_OK;
    std::atomic_bool Closed{false};
    QString Opened;
    HRESULT StartApplication(const UString &, const UString &, QWidget *) override {
        return E_NOTIMPL;
    }
    IQtProcessServices *ProcessServices() override {
        return FailSnapshot ? &_snapshot : _native.ProcessServices();
    }
    HRESULT CreateKnownProcess(const UString &program, const UString &parameters,
                               const UStringVector &paths, QWidget *window,
                               IQtProcessHandle *&process) override {
        Opened = ToQString(paths[0]);
        if (FailWait || FailName || FailSnapshot || FailAfterLaunch) {
            process = new CErrorHandle(Closed, FailName, FailSnapshot);
            if (FailAfterLaunch)
                throw std::bad_alloc();
            return LaunchResult;
        }
        return _native.CreateKnownProcess(program, parameters, paths, window, process);
    }
};
QString settings() { return qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"); }
QtOwnedTempEntry ownedDirectory(const QString &file) {
    const QString directory = QFileInfo(file).absolutePath();
    for (const auto &entry : QtOwnedTempRegistry().Entries())
        if (entry.path == directory)
            return entry;
    return {};
}
bool bind(CPanel &panel, IFolderManager *manager, IQtExternalFileServices &service,
          const QString &archive) {
    panel.AttachFolderManager(manager, settings());
    panel.AttachExternalFileServices(&service);
    COpenResult opened;
    if (panel.BindToPath(ToUString(archive), UString(), opened) != S_OK)
        return false;
    if (panel.GetFolder()->LoadItems() != S_OK)
        return false;
    UInt32 count = 0;
    return panel.GetFolder()->GetNumberOfItems(&count) == S_OK && count == 1;
}
} // namespace

class ArchiveExternalLifetimeTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void init() { QFile::remove(settings()); }
    void changedFilePublishesWithoutWindowUpdateRunner() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("изменённый файл 空.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("editor control");
        const auto stop = qScopeGuard([&] { write(control + ".exit", "exit"); });
        configure(control);
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        QtAssociatedFileServices service({});
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        qint64 publishedSize = -1;
        panel.AttachReloadPresentation([&](const CSelectedState &) -> HRESULT {
            RINOK(panel.GetFolder()->LoadItems())
            NWindows::NCOM::CPropVariant size;
            RINOK(panel.GetFolder()->GetProperty(0, kpidSize, &size))
            if (size.vt != VT_UI8)
                return E_FAIL;
            publishedSize = qint64(size.uhVal.QuadPart);
            return S_OK;
        });
        panel.EditItem(0, true);
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        QStringList errors;
        bool asked = false;
        QEventLoop delivery;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                if (box->objectName() == "externalUpdatePrompt") {
                    asked = true;
                    connect(box, &QObject::destroyed, &delivery, &QEventLoop::quit);
                    box->button(QMessageBox::Yes)->click();
                } else {
                    errors.append(box->text());
                    box->accept();
                }
            }
        });
        responder.start(1);
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTimer::singleShot(5000, &delivery, &QEventLoop::quit);
        if (!asked)
            delivery.exec();
        QVERIFY(asked);
        QTRY_COMPARE(publishedSize, qint64(modified.size()));
        QTRY_VERIFY(!QFileInfo(temporary).exists());
        panel.ExitExternalWorkers(false);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(contents(archive, name), modified);
    }
    void unknownFileFallback_data() {
        QTest::addColumn<bool>("known");
        QTest::addColumn<bool>("ask");
        QTest::addColumn<bool>("internal");
        QTest::addColumn<bool>("accept");
        QTest::newRow("unknown-cancel-default") << false << true << true << false;
        QTest::newRow("native-unknown-cancel") << false << true << true << false;
        QTest::newRow("unknown-open") << false << true << true << true;
        QTest::newRow("known-opens-directly") << true << true << true << false;
        QTest::newRow("question-disabled") << false << false << true << false;
        QTest::newRow("explicit-open-outside") << false << true << false << false;
    }
    void unknownFileFallback() {
        QFETCH(bool, known);
        QFETCH(bool, ask);
        QFETCH(bool, internal);
        QFETCH(bool, accept);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("неизвестный файл.sz-unknown-fixture");
        const QString archive = fixture.filePath("original.zip");
        const QByteArray contents("\0\xff\0\x81\0", 5);
        QVERIFY(write(fixture.filePath(name), contents));
        QVERIFY(createArchive(fixture.path(), archive, name));
        if (!ask)
            SaveConfirmUnknownFileOpen(false, settings());
        QString opened;
        QString probed;
        const auto cleanup = qScopeGuard([&] {
            if (!opened.isEmpty())
                QDir(QFileInfo(opened).absolutePath()).removeRecursively();
        });
        std::function<bool(const QString &)> probe;
        if (QByteArray(QTest::currentDataTag()) != "native-unknown-cancel")
            probe = [&](const QString &path) {
                probed = path;
                return known;
            };
        QtAssociatedFileServices service(
            [&](const QUrl &url) {
                opened = url.toLocalFile();
                return true;
            },
            {}, {}, probe);
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        bool prompted = false;
        bool cancelDefault = false;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                if (box->objectName() == "unknownFileOpenPrompt") {
                    prompted = true;
                    cancelDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
                    box->done(accept ? QMessageBox::Open : QMessageBox::Cancel);
                }
            }
        });
        responder.start(1);
        panel.OpenItem(0, internal, true);
        responder.stop();
        const bool expectedPrompt = ask && internal && !known;
        QCOMPARE(prompted, expectedPrompt);
        if (prompted)
            QVERIFY(cancelDefault);
        QCOMPARE(!opened.isEmpty(), !expectedPrompt || accept);
        if (!opened.isEmpty())
            QCOMPARE(read(opened), contents);
        else if (probe) {
            QVERIFY(!probed.isEmpty());
            QVERIFY(!QFileInfo::exists(probed));
            QVERIFY(ownedDirectory(probed).key.isEmpty());
        }
    }
    void childOutlivesParentAndKeepsCopyUntilItsExit() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("дочерний редактор.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("child control");
        const auto stop = qScopeGuard([&] { write(control + ".exit", "exit"); });
        configure(control, "--archive-editor-parent");
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        QtAssociatedFileServices service({});
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        panel.EditItem(0, true);
        QTRY_VERIFY(QFileInfo(control + ".parent-finished").exists());
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        QVERIFY(QFileInfo(temporary).isFile());
        const auto owned = ownedDirectory(temporary);
        QVERIFY(!owned.key.isEmpty());
        QVERIFY(owned.active && owned.externallyProtected);
        QVERIFY(!QtOwnedTempRegistry().ClaimForDeletion(owned));
        bool cleanupProtected = false;
        QTimer browseResponder;
        connect(&browseResponder, &QTimer::timeout, this, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(widgetDialogs_.activeDialog());
            if (!dialog)
                return;
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            const auto rows = list->findItems(QFileInfo(owned.path).fileName(), Qt::MatchExactly);
            if (!rows.isEmpty()) {
                list->clearSelection();
                list->setCurrentItem(rows.first());
                rows.first()->setSelected(true);
                cleanupProtected = !dialog->findChild<QPushButton *>("tempDelete")->isEnabled() &&
                                   !rows.first()->toolTip(0).isEmpty();
            }
            dialog->reject();
            browseResponder.stop();
        });
        browseResponder.start(1);
        MyBrowseForTempFolder(nullptr);
        browseResponder.stop();
        QVERIFY(cleanupProtected);
        QVERIFY(QFileInfo(temporary).isFile());
        bool prompt = false;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                if (box->objectName() == "externalUpdatePrompt") {
                    prompt = true;
                    box->done(QMessageBox::No);
                }
            }
        });
        responder.start(1);
        // No completion prompt while the child still owns this copy.
        QTest::qWait(100);
        QVERIFY(!prompt);
        QVERIFY(QFileInfo(temporary).isFile());
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo(control + ".finished").exists());
        QTRY_VERIFY(prompt);
        QTRY_VERIFY(!QFileInfo(temporary).exists());
        QCOMPARE(contents(archive, name), QByteArray("original"));
        panel.ExitExternalWorkers(false);
    }
    void unchangedLifetime_data() {
        QTest::addColumn<bool>("longRunning");
        QTest::newRow("short-kept-until-owner-exit") << false;
        QTest::newRow("long-deleted-on-process-exit") << true;
    }
    void unchangedLifetime() {
        QFETCH(bool, longRunning);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("неизменённый файл.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("control");
        const auto stop = qScopeGuard([&] { write(control + ".exit", "exit"); });
        configure(control);
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        QtAssociatedFileServices service({});
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        panel.EditItem(0, false);
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        if (longRunning)
            QTest::qWait(2100);
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo(control + ".finished").exists());
        if (longRunning)
            QTRY_VERIFY(!QFileInfo(temporary).exists());
        else {
            QTest::qWait(100);
            QVERIFY(QFileInfo(temporary).isFile());
        }
        panel.ExitExternalWorkers(false);
        QVERIFY(!QFileInfo(temporary).exists());
        QCOMPARE(contents(archive, name), QByteArray("original"));
    }
    void launchAndWaitFailures_data() {
        QTest::addColumn<bool>("waitError");
        QTest::addColumn<HRESULT>("launchResult");
        QTest::newRow("missing-program-cleanup") << false << HRESULT(S_OK);
        QTest::newRow("wait-error-preserves-copy") << true << HRESULT(S_OK);
        QTest::newRow("launch-error-with-handle-preserves-copy") << true << HRESULT(E_FAIL);
    }
    void launchAndWaitFailures() {
        QFETCH(bool, waitError);
        QFETCH(HRESULT, launchResult);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("ошибка запуска.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        SaveRegEditor(true, ToUString(fixture.filePath("missing program")), settings());
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CProcessService service;
        service.FailWait = waitError;
        service.LaunchResult = launchResult;
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        bool error = false;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                error = box->objectName() == "externalEditorError" &&
                        box->textFormat() == Qt::PlainText;
                box->accept();
            }
        });
        responder.start(1);
        panel.EditItem(0, true);
        responder.stop();
        QVERIFY(!service.Opened.isEmpty());
        if (waitError)
            QTRY_VERIFY(service.Closed.load());
        panel.ExitExternalWorkers(false);
        QCOMPARE(error, !waitError || launchResult != S_OK);
        QCOMPARE(QFileInfo(service.Opened).exists(), waitError);
        if (waitError) {
            const auto owned = ownedDirectory(service.Opened);
            QVERIFY(!owned.key.isEmpty());
            QVERIFY(!owned.active && owned.externallyProtected);
            QVERIFY(!QtOwnedTempRegistry().ClaimForDeletion(owned));
        }
        QCOMPARE(contents(archive, name), QByteArray("original"));
        if (waitError)
            QVERIFY(QDir(QFileInfo(service.Opened).absolutePath()).removeRecursively());
    }
    void registrationExceptions_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("main-handle-name") << 0;
        QTest::newRow("worker-snapshot") << 1;
        QTest::newRow("launch-with-handle") << 2;
    }
    void registrationExceptions() {
        QFETCH(int, failure);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("ошибка владельца.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        SaveRegEditor(true, L"fixture-editor", settings());
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CProcessService service;
        service.FailName = failure == 0;
        service.FailSnapshot = failure == 1;
        service.FailAfterLaunch = failure == 2;
        CPanel panel;
        QVERIFY(bind(panel, manager, service, archive));
        const auto removeCopy = qScopeGuard([&] {
            panel.ExitExternalWorkers(false);
            if (!service.Opened.isEmpty())
                QDir(QFileInfo(service.Opened).absolutePath()).removeRecursively();
        });
        if (failure != 1) {
            QVERIFY_THROWS_EXCEPTION(std::bad_alloc, panel.EditItem(0, true));
        } else {
            panel.EditItem(0, true);
        }
        QTRY_VERIFY(service.Closed.load());
        panel.ExitExternalWorkers(false);
        QVERIFY(!service.Opened.isEmpty());
        QCOMPARE(read(service.Opened), QByteArray("original"));
        const auto owned = ownedDirectory(service.Opened);
        QVERIFY(!owned.key.isEmpty());
        QVERIFY(!owned.active && owned.externallyProtected);
        QVERIFY(!QtOwnedTempRegistry().ClaimForDeletion(owned));
        QCOMPARE(contents(archive, name), QByteArray("original"));
    }
};
int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    ArchiveExternalLifetimeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "archive_external_lifetime_tests.moc"
