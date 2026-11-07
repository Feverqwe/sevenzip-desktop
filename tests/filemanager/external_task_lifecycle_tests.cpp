#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "filemanager/window/qtlocalfiletask.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <algorithm>

using namespace ExternalEditorFixture;

class ExternalTaskLifecycleTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void closingWindowUsesTheExistingWorkerShutdown_data() {
        QTest::addColumn<bool>("duringCopyChoice");
        QTest::newRow("last-file-window") << false;
        QTest::newRow("close-during-copy-choice") << true;
    }
    void closingWindowUsesTheExistingWorkerShutdown() {
        QFETCH(bool, duringCopyChoice);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("edit 空.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const auto archive = fixture.filePath("source.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const auto original = read(archive);
        const auto control = fixture.filePath("editor");
        const auto cleanup = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend, {}, nullptr, nullptr, DeviceSelectionServices::system(),
                          opener(control));
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "openOutsideAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const auto copy = QString::fromUtf8(read(control + ".ready"));
        if (duringCopyChoice) {
            auto *task = QtExternalFileTask::Active().front();
            task->Show();
            QDialog *details = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "externalFileDetails" && widget->isVisible())
                    details = qobject_cast<QDialog *>(widget);
            QVERIFY(details);
            bool copySeen = false, closed = false;
            dialogs_.respond(window, [&](QDialog &dialog) {
                copySeen = dialog.objectName() == "copyDialog";
                if (copySeen)
                    closed = window.close();
                else
                    dialog.reject();
            });
            details->findChild<QPushButton *>("externalFileSaveCopy")->click();
            QVERIFY(copySeen && closed && !window.isVisible());
        } else {
            const bool previous = qApp->quitOnLastWindowClosed();
            const auto restore = qScopeGuard([&] { qApp->setQuitOnLastWindowClosed(previous); });
            qApp->setQuitOnLastWindowClosed(true);
            bool closed = false, timedOut = false;
            QTimer deadline;
            deadline.setSingleShot(true);
            connect(&deadline, &QTimer::timeout, qApp, [&] {
                timedOut = true;
                qApp->quit();
            });
            QTimer::singleShot(0, &window, [&] { closed = window.close(); });
            deadline.start(5000);
            QCOMPARE(qApp->exec(), 0);
            QVERIFY(closed && !timedOut);
        }
        QTRY_VERIFY(QtExternalFileTask::Active().isEmpty());
        QVERIFY(!QFileInfo::exists(control + ".finished"));
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo::exists(control + ".finished"));
        QCOMPARE(read(copy), modified);
        QCOMPARE(read(archive), original);
        const QString directory = QFileInfo(copy).absolutePath();
        bool protectedCopy = false;
        QtOwnedTempRegistry registry;
        for (const auto &entry : registry.Entries())
            if (entry.path == directory) {
                protectedCopy = entry.externallyProtected && !registry.ClaimForDeletion(entry);
                auto pin = registry.PinForRead(entry);
                QVERIFY(pin);
                QVERIFY(QDir(directory).removeRecursively());
            }
        QVERIFY(protectedCopy);
    }
    void editedFileWhileWindowIsOpen_data() {
        QTest::addColumn<bool>("update");
        QTest::addColumn<bool>("stopDuringPrompt");
        QTest::addColumn<bool>("fast");
        QTest::addColumn<int>("exportChoice");
        QTest::newRow("update-while-window-open") << true << false << false << 0;
        QTest::newRow("later-while-window-open") << false << false << false << 0;
        QTest::newRow("stop-during-delivered-callback") << false << true << false << 0;
        QTest::newRow("fast-editor-keeps-protected-copy") << false << false << true << 0;
        QTest::newRow("stop-during-export-choice") << false << false << false << 1;
        QTest::newRow("export-keeps-source-copy") << false << false << false << 2;
    }
    void editedFileWhileWindowIsOpen() {
        QFETCH(bool, update);
        QFETCH(bool, stopDuringPrompt);
        QFETCH(bool, fast);
        QFETCH(int, exportChoice);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("изменить 空.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("archive.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("editor control");
        const auto stop = qScopeGuard([&] {
            write(control + ".exit", "exit");
            for (auto *task : QtExternalFileTask::Active())
                task->Stop();
        });
        auto backend = std::make_unique<ArchiveBackend>();
        auto window = std::make_unique<MainWindow>(
            backend.get(), std::function<bool(const QUrl &)>(), nullptr, nullptr,
            DeviceSelectionServices::system(), opener(control));
        window->show();
        window->openArchive(archive);
        QTRY_COMPARE(table(*window)->rowCount(), 1);
        table(*window)->selectRow(0);
        table(*window)->setCurrentCell(0, 0);
        action(*window, "openOutsideAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString copy = QString::fromUtf8(read(control + ".ready"));
        QCOMPARE(read(copy), QByteArray("original"));
        QVERIFY(!QtExternalFileTask::Active().isEmpty());
        QCOMPARE(read(copy), QByteArray("original"));
        const QByteArray accepted = read(archive);
        bool asked = false;
        bool copyDialogSeen = false, manuallyPublish = false;
        const QString exported = fixture.filePath("exported edit.txt");
        QEventLoop delivered;
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            auto *dialog = dialogs_.activeDialog();
            if (dialog && dialog->objectName() == "externalUpdatePrompt") {
                asked = true;
                if (stopDuringPrompt) {
                    for (auto *task : QtExternalFileTask::Active())
                        task->Stop();
                } else if (exportChoice)
                    dialog->findChild<QPushButton *>("externalUpdateSaveCopy")->click();
                else if (update || manuallyPublish)
                    dialog->findChild<QPushButton *>("externalUpdateCommit")->click();
                else
                    QTest::keyClick(dialog, Qt::Key_Escape);
            } else if (dialog && dialog->objectName() == "copyDialog" && exportChoice) {
                copyDialogSeen = true;
                if (exportChoice == 1) {
                    for (auto *task : QtExternalFileTask::Active())
                        task->Stop();
                } else {
                    dialog->findChild<QLineEdit *>("copyDestination")->setText(exported);
                    dialog->accept();
                }
            }
            if (asked && QtExternalFileTask::Active().isEmpty())
                delivered.quit();
        });
        responder.start(1);
        if (!fast)
            waitForKnownEditor();
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTimer done;
        connect(&done, &QTimer::timeout, &delivered, [&] {
            if (fast && QtExternalFileTask::Active().isEmpty())
                delivered.quit();
        });
        done.start(5);
        QTimer::singleShot(15000, &delivered, &QEventLoop::quit);
        delivered.exec();
        QCOMPARE(asked, !fast);
        QVERIFY(QtExternalFileTask::Active().isEmpty());
        QCOMPARE(copyDialogSeen, exportChoice != 0);
        if (exportChoice == 2) {
            QTRY_COMPARE(read(exported), modified);
            QTRY_VERIFY(QtLocalFileTask::Active().isEmpty());
        }
        QTRY_VERIFY(QFileInfo::exists(control + ".finished"));
        if (update) {
            QCOMPARE(contents(archive, name), modified);
            QVERIFY(!QFileInfo::exists(copy));
        } else {
            QCOMPARE(read(archive), accepted);
            QCOMPARE(read(copy), modified);
            const QString directory = QFileInfo(copy).absolutePath();
            QtOwnedTempRegistry registry;
            if (fast) {
                QDialog *details = nullptr;
                for (auto *widget : QApplication::topLevelWidgets())
                    if (widget->objectName() == "externalFileDetails")
                        details = qobject_cast<QDialog *>(widget);
                QVERIFY(details);
                auto *check = details->findChild<QPushButton *>("externalFileCheck");
                QVERIFY(check);
                QVERIFY(!check->isEnabled());
                auto *confirmed = details->findChild<QCheckBox *>("externalFileConfirmedClosed");
                QVERIFY(confirmed && confirmed->isVisible());
                confirmed->setChecked(true);
                QVERIFY(check->isEnabled());
                auto *path = details->findChild<QLabel *>("externalFilePath");
                QVERIFY(path);
                QCOMPARE(path->text(), QFileInfo(copy).fileName());
                QCOMPARE(path->toolTip(), copy);
                auto *identity = details->findChild<QWidget *>("externalFileIdentity");
                QVERIFY(identity);
                QCOMPARE(identity->accessibleName(), QFileInfo(name).fileName());
                QVERIFY(identity->toolTip().contains(archive));
                for (const auto &locale : {QString("-"), QString("ru")}) {
                    SaveRegLang(ToUString(locale), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
                    ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
                    for (const auto &frame : {QString("normal"), QString("compact")}) {
                        details->resize(frame == "compact" ? QSize(360, 260) : QSize(520, 420));
                        QApplication::processEvents();
                        auto *scroll = details->findChild<QScrollArea *>("dialogContentScroll");
                        QVERIFY(scroll);
                        scroll->ensureWidgetVisible(path);
                        QApplication::processEvents();
                        QTRY_VERIFY2(UiInspection::labelFits(*path),
                                     qPrintable(QString("%1/%2: %3x%4, required height %5")
                                                    .arg(locale, frame)
                                                    .arg(path->width())
                                                    .arg(path->height())
                                                    .arg(path->heightForWidth(path->width()))));
                        auto *footer = details->findChild<QDialogButtonBox *>();
                        QVERIFY(footer && UiInspection::fullyVisible(*footer, *details));
                        for (auto *button : footer->buttons())
                            if (button->isVisible())
                                QVERIFY(UiInspection::fullyVisible(*button, *details));
                        QVERIFY(confirmed->isChecked() && check->isEnabled());
                        QCOMPARE(path->toolTip(), copy);
                        QVERIFY(UiInspection::writeReport(*details, "external-details-" + locale +
                                                                        '-' + frame));
                    }
                }
                const auto entries = registry.Entries();
                const auto entry =
                    std::find_if(entries.begin(), entries.end(),
                                 [&](const auto &item) { return item.path == directory; });
                QVERIFY(entry != entries.end());
                QVERIFY(!entry->active);
                QVERIFY(entry->externallyProtected);
                QVERIFY(!registry.ClaimForDeletion(*entry));
                manuallyPublish = true;
                check->click();
                QCOMPARE(contents(archive, name), modified);
                QVERIFY(!registry.ClaimForDeletion(*entry));
                QVERIFY(write(copy, modified + "more changes"));
                auto pin = registry.PinForRead(*entry);
                QVERIFY(pin);
                // This fixture launched our already-finished helper, so its own
                // protected bytes can be removed after the behavioral checks.
                QVERIFY(QDir(directory).removeRecursively());
                pin.reset();
                details->close();
                return;
            }
            bool removed = false;
            for (const auto &entry : registry.Entries()) {
                if (entry.path != directory)
                    continue;
                auto claim = registry.ClaimForDeletion(entry);
                QVERIFY(claim);
                removed = QDir(directory).removeRecursively();
            }
            QVERIFY(removed);
        }
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == "externalFileDetails")
                widget->close();
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    ExternalTaskLifecycleTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_task_lifecycle_tests.moc"
