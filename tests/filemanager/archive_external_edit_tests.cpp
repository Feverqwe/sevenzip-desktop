#include "7zip/PropID.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "Common/StringConvert.h"
#include "filemanager/support/panel_load_folder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtexternalfiletask.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/temp/qtownedtempregistry.h"
#include "settings/RegistryUtils.h"
#include "support/widget_dialogs.h"
#include "ui/lifecycle/qttaskpresentation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTreeWidget>

using namespace ExternalEditorFixture;

class ArchiveExternalEditTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;

    QDialog *updateDialog() const {
#ifdef Q_OS_WIN
        // A modal widget can be shown without desktop activation on the runner.
        return qobject_cast<QDialog *>(QApplication::activeModalWidget());
#else
        return widgetDialogs_.activeDialog();
#endif
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void associationWithoutProcessShowsActionsOnlyAfterSave_data() {
        QTest::addColumn<bool>("updateArchive");
        QTest::newRow("keep-copy") << false;
        QTest::newRow("update-archive") << true;
    }
    void associationWithoutProcessShowsActionsOnlyAfterSave() {
        QFETCH(bool, updateArchive);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("изменяемый файл.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        QString copy;
        const auto cleanup = qScopeGuard([&] {
            if (!copy.isEmpty())
                QDir(QFileInfo(copy).absolutePath()).removeRecursively();
        });
        ArchiveBackend backend;
        MainWindow window(&backend, [&](const QUrl &url) {
            copy = url.toLocalFile();
            return true;
        });
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "openOutsideAction")->trigger();
        QVERIFY(QFileInfo(copy).isFile());
        QTRY_VERIFY(!table(window)->sourcePanel->HasExternalWorkers());
        const auto details = []() -> QDialog * {
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "externalFileDetails" && widget->isVisible())
                    return qobject_cast<QDialog *>(widget);
            return nullptr;
        };
        QVERIFY(!details());
        QSaveFile saved(copy);
        QVERIFY(saved.open(QIODevice::WriteOnly));
        QCOMPARE(saved.write(modified), qint64(modified.size()));
        QVERIFY(saved.commit());
        QTRY_VERIFY(details());
        QVERIFY(details()->findChild<QPushButton *>("externalFileSaveCopy")->isEnabled());
        auto *update = details()->findChild<QPushButton *>("externalFileCheck");
        QVERIFY(update && update->isVisible());
        QCOMPARE(update->text(),
                 QCoreApplication::translate("QtExternalFileTask", "Update archive…"));
        QCOMPARE(contents(archive, name), QByteArray("original"));
        if (updateArchive) {
            auto *confirmed = details()->findChild<QCheckBox *>("externalFileConfirmedClosed");
            QVERIFY(confirmed && confirmed->isVisible());
            QVERIFY(!update->isEnabled());
            confirmed->click();
            QVERIFY(update->isEnabled());
            QTimer responder;
            widgetDialogs_.connectResponder(responder, &window, [&] {
                auto *question = updateDialog();
                if (question && question->objectName() == "externalUpdatePrompt")
                    question->findChild<QPushButton *>("externalUpdateCommit")->click();
            });
            responder.start(1);
            update->click();
            QTRY_COMPARE(contents(archive, name), modified);
            return;
        }
        details()->close();
        QVERIFY(window.close());
        QCOMPARE(read(copy), modified);
    }
    void unavailableFolderKeepsChangedFileWithoutReload() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString changed = fixture.filePath(QStringLiteral("изменённая копия 空.txt"));
        QVERIFY(write(changed, modified));
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        spec->folderPath = L"archive.zip/";
        CPanel panel(provider);
        spec->countResult = E_FAIL;
        // Failed outputs are not a usable listing, even when nonzero.
        spec->itemCount = 2;
        CTempFileInfo temp;
        temp.FileIndex = 0;
        temp.RelPath = spec->firstName;
        temp.FilePath = us2fs(changed.toStdWString().c_str());
        bool unexpectedMessage = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                unexpectedMessage = true;
                box->accept();
            }
        });
        responder.start(1);
        std::optional<CSelectedState> refresh = CSelectedState();
        QVERIFY(!panel.OnOpenItemChanged(temp, panel.GetCurrentFolderPrefix(), false, UString(),
                                         &refresh));
        QVERIFY(!refresh);
        QVERIFY(!unexpectedMessage);
        QCOMPARE(read(changed), modified);
    }
    void sourceViewerOpensLargeFileWithoutPreviewLimit() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("большой файл 密碼.bin");
        const QByteArray bytes(17 * 1024 * 1024, 'x');
        QVERIFY(write(fixture.filePath(name), bytes));
        const QString archive = fixture.filePath("large.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QByteArray originalArchive = read(archive);
        const QString control = fixture.filePath("viewer control");
        const auto stopViewer = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend, {}, nullptr, nullptr, DeviceSelectionServices::system(),
                          opener(control));
        QString unexpectedMessage;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog())) {
                unexpectedMessage = box->text();
                box->accept();
            }
        });
        responder.start(1);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        table(window)->setCurrentCell(0, 0);
        QTRY_VERIFY(action(window, "viewFileAction")->isEnabled());
        action(window, "viewFileAction")->trigger();
        QVERIFY2(unexpectedMessage.isEmpty(), qPrintable(unexpectedMessage));
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        QCOMPARE(read(temporary), bytes);
        // Original MyThreadFunction retains a short unchanged launch until
        // panel exit. Exercise its long-running process-completion branch.
        QTest::qWait(2100);
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(!QFileInfo::exists(temporary));
        QCOMPARE(read(archive), originalArchive);
    }
    void changedFileUsesUpdateDecision_data() {
        QTest::addColumn<int>("answer");
        QTest::addColumn<bool>("viewer");
        QTest::newRow("editor-yes") << int(QMessageBox::Yes) << false;
        QTest::newRow("editor-no") << int(QMessageBox::No) << false;
        QTest::newRow("editor-cancel") << int(QMessageBox::Cancel) << false;
        QTest::newRow("viewer-yes") << int(QMessageBox::Yes) << true;
    }
    void changedFileUsesUpdateDecision() {
        QFETCH(int, answer);
        QFETCH(bool, viewer);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("файл с пробелом.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("editor control");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        MainWindow window(&backend, {}, nullptr, nullptr, DeviceSelectionServices::system(),
                          opener(control));
        window.resize(viewer ? QSize(640, 400) : QSize(980, 620));
        window.show();
        window.activateWindow();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        window.activateWindow();
        QApplication::processEvents();
        table(window)->setFocus();
        QTRY_VERIFY(table(window)->hasFocus());
        QTRY_VERIFY(action(window, viewer ? "viewFileAction" : "editFileAction")->isEnabled());
        QTest::keyClick(table(window), viewer ? Qt::Key_F3 : Qt::Key_F4);
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        QVERIFY(QFileInfo(temporary).isFile());
        QCOMPARE(QFileInfo(temporary).fileName().normalized(QString::NormalizationForm_C), name);
        const QString temporaryDirectory = QFileInfo(temporary).absolutePath();
        const auto cleanup = qScopeGuard([&] {
            table(window)->sourcePanel->ExitExternalWorkers(false);
            if (QFileInfo::exists(control + ".finished"))
                QDir(temporaryDirectory).removeRecursively();
        });
        bool asked = false;
        bool geometry = false;
        QEventLoop promptDelivery;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, &window, [&] {
            auto *box = updateDialog();
            if (auto *progress = dynamic_cast<CProgressDialog *>(box);
                progress &&
                QtTaskPresentation::Application().snapshot().phase == QtOperationPhase::Reviewing) {
                progress->reject();
                return;
            }
            if (box && box->objectName() == "progressFinalMessage") {
                box->accept();
                return;
            }
            if (!asked && box && box->objectName() == "externalUpdatePrompt") {
                connect(box, &QObject::destroyed, &promptDelivery, &QEventLoop::quit);
                asked = true;
                geometry = true;
                for (auto *button : box->findChildren<QAbstractButton *>())
                    if (button->isVisible())
                        geometry &= UiInspection::fullyVisible(*button, *box);
                for (auto *label : box->findChildren<QLabel *>())
                    if (label->isVisible())
                        geometry &= UiInspection::fullyVisible(*label, *box) &&
                                    UiInspection::labelFits(*label);
                geometry &= UiInspection::writeReport(*box, QString("external-update-prompt-%1-%2")
                                                                .arg(viewer ? "compact" : "normal")
                                                                .arg(answer));
                if (answer == QMessageBox::Cancel)
                    QTest::keyClick(box, Qt::Key_Escape);
                else if (answer == QMessageBox::Yes)
                    box->findChild<QPushButton *>("externalUpdateCommit")->click();
                else
                    box->findChild<QPushButton *>("externalUpdateLater")->click();
            }
        });
        responder.start(1);
        waitForKnownEditor();
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        // Cocoa native modal delivery requires a running event loop, rather
        // than QTest's manual processEvents pump. Keep the same 5s wait budget.
        QTimer::singleShot(5000, &promptDelivery, &QEventLoop::quit);
        if (!asked)
            promptDelivery.exec();
        QVERIFY2(
            asked,
            qPrintable(QString("editor finished=%1, temporary bytes=%2, backend busy=%3, modal=%4")
                           .arg(QFileInfo::exists(control + ".finished"))
                           .arg(QFileInfo(temporary).size())
                           .arg(backend.busy())
                           .arg(QApplication::activeModalWidget()
                                    ? QApplication::activeModalWidget()->objectName()
                                    : QStringLiteral("none"))));
        QVERIFY(geometry);
        QTRY_VERIFY(QtExternalFileTask::Active().isEmpty());
        if (answer == QMessageBox::Yes)
            QVERIFY(!QDir(temporaryDirectory).exists());
        else {
            QCOMPARE(read(temporary), modified);
            QtOwnedTempEntry retained;
            const auto copyRetired = [&] {
                for (const auto &entry : QtOwnedTempRegistry().Entries())
                    if (entry.path == temporaryDirectory) {
                        retained = entry;
                        return !entry.active;
                    }
                return false;
            };
            QTRY_VERIFY(copyRetired());
            QVERIFY(!retained.externallyProtected);
        }
        responder.stop();
        QCOMPARE(contents(archive, name),
                 answer == QMessageBox::Yes ? modified : QByteArray("original"));
        QTRY_COMPARE(table(window)->rowCount(), 1);
        if (answer == QMessageBox::Yes) {
            QTRY_COMPARE(
                table(window)->model()->index(0, 2).data(QtPanelList::SortValueRole).toULongLong(),
                quint64(modified.size()));
            QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        }
    }
    void closingWhileKnownProcessRunsRetainsTemporaryCopy() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("путь с пробелом.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString control = fixture.filePath("control");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        ArchiveBackend backend;
        auto window = std::make_unique<MainWindow>(
            &backend, std::function<bool(const QUrl &)>(), nullptr, nullptr,
            DeviceSelectionServices::system(), opener(control));
        window->show();
        window->openArchive(archive);
        QTRY_COMPARE(table(*window)->rowCount(), 1);
        table(*window)->selectRow(0);
        action(*window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        const QString temporaryDirectory = QFileInfo(temporary).absolutePath();
        window.reset();
        QVERIFY(QFileInfo(temporary).isFile());
        bool asked = false;
        QEventLoop decision;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            if (auto *dialog = widgetDialogs_.activeDialog();
                dialog && dialog->objectName() == "externalUpdatePrompt") {
                asked = true;
                dialog->reject();
            }
            if (asked && QtExternalFileTask::Active().isEmpty())
                decision.quit();
        });
        responder.start(1);
        waitForKnownEditor();
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo(control + ".finished").exists());
        QTRY_VERIFY(QtExternalFileTask::Active().isEmpty());
        QVERIFY(!asked);
        QCOMPARE(read(temporary), modified);
        QCOMPARE(contents(archive, name), QByteArray("original"));
        QVERIFY(QDir(temporaryDirectory).removeRecursively());
    }
    void deferredUpdateAndOwnerChanges_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("wait-for-idle") << 0;
        QTest::newRow("close-pending") << 1;
        QTest::newRow("changed-location") << 2;
        QTest::newRow("other-focused-panel") << 3;
    }
    void deferredUpdateAndOwnerChanges() {
        QFETCH(int, outcome);
        const auto closeDetails = qScopeGuard([] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "externalFileDetails")
                    widget->close();
        });
        QTemporaryDir fixture;
        const QString name = QStringLiteral("отложенная правка.txt");
        const QString archive = fixture.filePath("original.zip");
        QVERIFY(write(fixture.filePath(name), "original"));
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QByteArray originalArchive = read(archive);
        const QString control = fixture.filePath("control");
        const QString peerPath = fixture.filePath("peer");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        if (outcome == 3) {
            QVERIFY(QDir().mkdir(peerPath));
            QVERIFY(write(QDir(peerPath).filePath("sentinel.txt"), "peer"));
            const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
            CWindowInfo info{};
            info.rect.right = 980;
            info.rect.bottom = 620;
            info.numPanels = 2;
            info.splitterPos = 490;
            info.Save(settings);
            SavePanelPath(1, ToUString(peerPath), settings);
        }
        ArchiveBackend backend;
        auto window = std::make_unique<MainWindow>(
            &backend, std::function<bool(const QUrl &)>(), nullptr, nullptr,
            DeviceSelectionServices::system(), opener(control));
        window->show();
        auto *leftPanel = window->findChild<QWidget *>("filePanel0");
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        window->activateWindow();
        leftTable->setFocus();
        QTRY_COMPARE(window->focusWidget(), leftTable);
        window->openArchive(archive);
        QTRY_COMPARE(leftTable->rowCount(), 1);
        leftTable->selectRow(0);
        action(*window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        QLineEdit *rightAddress = nullptr;
        const auto displayedSize = [leftTable, &name] {
            const int sizeColumn = leftTable->columnForProperty(kpidSize);
            const auto *model = leftTable->model();
            for (int row = 0; row < model->rowCount(); ++row)
                if (model->index(row, 0)
                        .data(QtPanelList::NameRole)
                        .toString()
                        .normalized(QString::NormalizationForm_C) == name)
                    return model->index(row, sizeColumn).data(QtPanelList::SortValueRole);
            return QVariant();
        };
        if (outcome == 3) {
            QVERIFY(action(*window, "twoPanelsAction")->isChecked());
            rightAddress =
                window->findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
            QTRY_COMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                         QFileInfo(peerPath).canonicalFilePath());
        }
        if (outcome == 2) {
            window->openPath(fixture.path());
        }
        if (auto *focused = QApplication::focusWidget())
            focused->clearFocus();
        QDialog blocker;
        blocker.setWindowModality(Qt::ApplicationModal);
        blocker.show();
        QTRY_COMPARE(QApplication::activeModalWidget(), &blocker);
        bool asked = false;
        QEventLoop completion;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, this, [&] {
            auto *box = updateDialog();
            if (auto *progress = dynamic_cast<CProgressDialog *>(box);
                progress &&
                QtTaskPresentation::Application().snapshot().phase == QtOperationPhase::Reviewing) {
                progress->reject();
                return;
            }
            if (box) {
                if (box->objectName() == "externalUpdatePrompt") {
                    asked = true;
                    box->findChild<QPushButton *>("externalUpdateCommit")->click();
                } else if (box->objectName() == "progressFinalMessage")
                    box->accept();
            }
            if (asked && QtExternalFileTask::Active().isEmpty() &&
                ((outcome != 0 && outcome != 3) ||
                 displayedSize().toULongLong() == quint64(modified.size())))
                completion.quit();
        });
        responder.start(1);
        waitForKnownEditor();
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QTRY_VERIFY(QFileInfo(control + ".finished").exists());
        QTest::qWait(100);
        QVERIFY(!asked);
        QVERIFY(QFileInfo(temporary).isFile());
        if (outcome == 1) {
            window.reset();
        }
        blocker.close();
        if (window && (outcome == 0 || outcome == 3)) {
            auto *panel = outcome == 3 ? window->findChild<QWidget *>("filePanel1") : leftPanel;
            auto *table = panel->findChild<FileTable *>("archiveTable");
            window->activateWindow();
            table->setFocus();
            QTRY_COMPARE(window->focusWidget(), table);
        }
        if (outcome == 1) {
            QTRY_VERIFY(QtExternalFileTask::Active().isEmpty());
            QVERIFY(!asked);
            QCOMPARE(read(archive), originalArchive);
            QCOMPARE(read(temporary), modified);
            QVERIFY(QDir(QFileInfo(temporary).absolutePath()).removeRecursively());
            return;
        }
        QTimer::singleShot(8000, &completion, &QEventLoop::quit);
        completion.exec();
        QVERIFY(asked);
        QVERIFY(QtExternalFileTask::Active().isEmpty());
        {
            QVERIFY(!QFileInfo(temporary).exists());
            QCOMPARE(contents(archive, name), modified);
            if (outcome == 3)
                QCOMPARE(QFileInfo(rightAddress->text()).canonicalFilePath(),
                         QFileInfo(peerPath).canonicalFilePath());
            if ((outcome == 0 || outcome == 3) && !displayedSize().isValid()) {
                const auto *source = leftTable->sourcePanel;
                qWarning("Archive table: %d rows, size column %d, archive %d, bound %d, ready %d",
                         leftTable->rowCount(), leftTable->columnForProperty(kpidSize),
                         source->IsArcFolder(), source->GetFolder() == leftTable->textFolder,
                         source->ShouldProcessNotifications());
            }
            if (outcome == 0 || outcome == 3)
                QCOMPARE(displayedSize(), QVariant::fromValue(quint64(modified.size())));
        }
        responder.stop();
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    ArchiveExternalEditTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "archive_external_edit_tests.moc"
