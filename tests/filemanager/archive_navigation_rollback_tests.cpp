#include "archive/support/native_archive_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "platform/support/external_editor_fixture.h"
#include "platform/support/native_property_failure_runtime.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QScrollBar>

class ArchiveNavigationRollbackTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void nativeOpenKeepsFirstPublicationFailure_data() {
        QTest::addColumn<quint32>("failedIndex");
        QTest::addColumn<qint32>("failure");
        QTest::newRow("initial-error") << quint32(0) << qint32(E_FAIL);
        QTest::newRow("deferred-error") << quint32(257) << qint32(E_FAIL);
        QTest::newRow("initial-cancel") << quint32(0) << qint32(E_ABORT);
        QTest::newRow("deferred-cancel") << quint32(257) << qint32(E_ABORT);
    }
    void nativeOpenKeepsFirstPublicationFailure() {
        QFETCH(quint32, failedIndex);
        QFETCH(qint32, failure);
        NativePropertyFailureRuntime runtime;
        QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QTemporaryDir fixture;
        const QString candidate = fixture.filePath("candidate.fixture7z");
        QVERIFY(runtime.createArchive(candidate, 301));
        ArchiveBackend backend(nullptr, runtime.engine());
        MainWindow window(&backend);
        window.show();
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                message->accept();
        });
        responder.start(5);
        window.openPath(fixture.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled());
        auto &panel = *files->sourcePanel;
        const auto *provider = panel.GetFolder();
        const UString prefix = panel.GetCurrentFolderPrefix();
        const int row = filesystemRow(window, "candidate.fixture7z");
        QVERIFY(row >= 0);
        files->selectRow(row);
        files->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        QVERIFY(runtime.arm(failedIndex, HRESULT(failure), 301));
        action(window, "openItemAction")->trigger();
        QTRY_VERIFY(files->isEnabled());
        QVERIFY(!runtime.receipts().isEmpty());
        QCOMPARE(panel.GetFolder(), provider);
        QCOMPARE(panel.GetCurrentFolderPrefix(), prefix);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(),
                 "candidate.fixture7z");
        QVERIFY(files->selectionModel()->isRowSelected(files->currentRow(), {}));
        QVERIFY(!action(window, "forwardAction")->isEnabled());
        QVERIFY(!QSettings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat)
                     .value("FolderHistory")
                     .toStringList()
                     .contains(candidate + '/'));
    }
    void savedArchiveRejectsDeferredMetadataFailureAndRetries() {
        NativePropertyFailureRuntime runtime;
        QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("saved archive.fixture7z");
        QVERIFY(runtime.createArchive(archive, 301));
        ArchiveBackend backend(nullptr, runtime.engine());
        MainWindow window(&backend);
        window.show();
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                message->accept();
        });
        answer.start(5);
        const auto active = [&] {
            for (auto *files : window.findChildren<FileTable *>())
                if (files->isVisible())
                    return files;
            return static_cast<FileTable *>(nullptr);
        };
        QtWindowWorkspace *workspace = nullptr;
        for (auto *object : window.children())
            if (auto *owner = dynamic_cast<QtWindowWorkspace *>(object))
                workspace = owner;
        QVERIFY(workspace);
        window.openArchive(archive);
        QTRY_VERIFY(active() && active()->isEnabled() && active()->rowCount() == 301);
        const QString requested = ToQString(active()->sourcePanel->GetCurrentFolderPrefix());
        const auto saved = workspace->Save();
        window.openPath(fixture.path());
        QTRY_VERIFY(active()->isEnabled() && active()->sourcePanel->IsFSFolder());
        QVERIFY(runtime.arm(257, E_FAIL, 301));
        bool finished = false;
        workspace->Restore(saved, [&] { finished = true; });
        QTRY_VERIFY(finished);
        QVERIFY(!runtime.receipts().isEmpty());
        QCOMPARE(active()->rowCount(), 0);
        const QString error =
            QCoreApplication::translate("QtWindowWorkspace", "Location unavailable: %1")
                .arg(requested);
        bool errorVisible = false;
        for (auto *label : window.findChildren<QLabel *>())
            errorVisible |= label->isVisible() && label->text() == error;
        QVERIFY(errorVisible);
        QVERIFY(runtime.clear());
        QPushButton *retry = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->isVisible() &&
                button->text() == QCoreApplication::translate("QtWindowWorkspace", "Retry"))
                retry = button;
        QVERIFY(retry);
        QTest::mouseClick(retry, Qt::LeftButton);
        QTRY_VERIFY(active()->isEnabled() && active()->rowCount() == 301);
    }
    void physicalOpeningKeepsOrReleasesNestedContext_data() {
        QTest::addColumn<bool>("materialized");
        QTest::addColumn<QString>("outcome");
        for (bool materialized : {false, true})
            for (const QString &outcome :
                 {QString("refused"), QString("cancel"), QString("success")})
                QTest::newRow(qPrintable(QString("%1-%2").arg(materialized).arg(outcome)))
                    << materialized << outcome;
        QTest::newRow("modified-parent") << true << QString("modified");
    }
    void physicalOpeningKeepsOrReleasesNestedContext() {
        QFETCH(bool, materialized);
        QFETCH(QString, outcome);
        const bool accepted = outcome == "success" || outcome == "modified";
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString password = QStringLiteral(" local 密碼 ");
        QList<NativeArchiveFixture::Item> entries;
        for (int index = 0; index < 300; ++index) {
            NativeArchiveFixture::Item item;
            item.Path = QString("entry-%1.txt").arg(index, 4, 10, QChar('0'));
            item.Bytes = QByteArray("payload ") + QByteArray::number(index);
            entries.append(item);
        }
        const QString innerName = QStringLiteral("inner 空.7z");
        const QString inner = fixture.filePath(innerName);
        QVERIFY(NativeArchiveFixture::Write(inner, entries, "7z", password));
        const QString outer = fixture.filePath("outer archive.tar");
        NativeArchiveFixture::Item nested;
        nested.Path = innerName;
        nested.Bytes = ExternalEditorFixture::read(inner);
        QVERIFY(NativeArchiveFixture::Write(outer, {nested}, "tar"));
        // Exercise prefix growth beyond the nested source's existing buffer.
        const QString target = fixture.filePath(QString(180, QChar('t')) + " archive.7z");
        if (outcome == "refused")
            QVERIFY(ExternalEditorFixture::write(target, "not an archive"));
        else {
            QList<NativeArchiveFixture::Item> candidate;
            for (int index = 0; index < (accepted ? 300 : 1); ++index) {
                NativeArchiveFixture::Item item;
                item.Path = QString("new-%1.txt").arg(index, 4, 10, QChar('0'));
                item.Bytes = "new source";
                candidate.append(item);
            }
            QVERIFY(NativeArchiveFixture::Write(target, candidate, "7z",
                                                outcome == "cancel" ? QString("candidate password")
                                                                    : QString()));
        }
        ArchiveBackend backend;
        QCOMPARE(NativeArchiveFixture::Attach(backend), S_OK);
        MainWindow window(&backend);
        window.resize(640, 400);
        window.show();
        int prompts = 0;
        int parentPrompts = 0;
        bool rejectPassword = false;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            auto *modal = dialogs_.activeDialog();
            if (auto *dialog = dynamic_cast<CPasswordDialog *>(modal)) {
                ++prompts;
                if (rejectPassword)
                    dialog->reject();
                else {
                    dialog->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    dialog->accept();
                }
            } else if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                if (message->objectName() == "modifiedArchiveQuestion") {
                    ++parentPrompts;
                    message->button(QMessageBox::Yes)->click();
                } else
                    QTest::keyClick(message, Qt::Key_Return);
            }
        });
        responder.start(5);
        window.openArchive(outer);
        auto *files = table(window);
        QTRY_COMPARE(files->rowCount(), 1);
        auto &panel = *files->sourcePanel;
        QString ownedPath;
        if (materialized) {
            const QString directory = fixture.filePath("owned materialized");
            QVERIFY(QDir().mkpath(directory));
            ownedPath = directory + '/' + innerName;
            QVERIFY(QFile::copy(inner, ownedPath));
            QtOwnedTempRegistry registry(fixture.filePath("registry"));
            CTempFileInfo native;
            native.FilePath = us2fs(ToUString(ownedPath));
            native.FolderPath = us2fs(ToUString(directory + '/'));
            native.RelPath = ToUString(innerName);
            native.FileIndex = 0;
            native.NeedDelete = true;
            native.OwnedTempLease = registry.RegisterCreated(ownedPath);
            QVERIFY(native.OwnedTempLease);
            COpenResult opened;
            panel.LoadFullPath();
            const UString path = panel.GetCurrentFolderPrefix() + ToUString(innerName);
            QCOMPARE(panel.OpenAsArc(nullptr, native, path, UString(), opened), S_OK);
            native.OwnedTempLease.reset();
            QCOMPARE(panel.RefreshListCtrl(), S_OK);
        } else
            openRow(window, 0);
        QTRY_COMPARE(files->rowCount(), entries.size());
        QTRY_VERIFY(files->isEnabled());
        QCOMPARE(prompts, 1);
        QCOMPARE(panel.GetParentFolders().Size(), 2u);
        QCOMPARE(panel.GetParentFolders().Back().IsVirtual, !materialized);
        if (materialized)
            QVERIFY(panel.GetParentFolders().Back().OwnedTempLease);
        const auto *previousFolder = panel.GetFolder();
        panel.LoadFullPath();
        const UString previousPath = panel.GetCurrentFolderPrefix();
        files->selectRow(200);
        files->setCurrentCell(200, 0, QItemSelectionModel::NoUpdate);
        const QString selectedName = files->model()->index(200, 0).data().toString();
        const auto selected = std::find_if(entries.cbegin(), entries.cend(), [&](const auto &item) {
            return item.Path == selectedName;
        });
        QVERIFY(selected != entries.cend());
        const QByteArray selectedBytes = selected->Bytes;
        QTRY_VERIFY(files->verticalScrollBar()->maximum() > 0);
        files->verticalScrollBar()->setValue(files->verticalScrollBar()->maximum() / 2);
        const int scroll = files->verticalScrollBar()->value();
        QByteArray modifiedBytes;
        if (outcome == "modified") {
            const QString replacement = fixture.filePath("replacement.7z");
            NativeArchiveFixture::Item changed;
            changed.Path = "changed.txt";
            changed.Bytes = "updated nested payload";
            QVERIFY(NativeArchiveFixture::Write(replacement, {changed}, "7z", password));
            modifiedBytes = ExternalEditorFixture::read(replacement);
            QVERIFY(QFile::remove(ownedPath));
            QVERIFY(QFile::copy(replacement, ownedPath));
        }
        rejectPassword = outcome == "cancel";
        auto *address = window.findChild<QLineEdit *>("archivePath");
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        address->setText(target);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(files->isEnabled());
        if (accepted) {
            QTRY_COMPARE(files->rowCount(), 300);
            // Offscreen can leave the window inactive after modal progress.
            // Preserve its intended focus without activating a background window.
            QTRY_COMPARE(window.focusWidget(), files);
            if (window.isActiveWindow())
                QVERIFY(files->hasFocus());
            QTRY_VERIFY(!address->isVisible());
            QCOMPARE(files->model()->index(0, 0).data().toString(), QString("new-0000.txt"));
            QCOMPARE(QDir::cleanPath(ToQString(panel.GetCurrentFolderPrefix())), target);
            QCOMPARE(panel.GetParentFolders().Size(), 1u);
            if (materialized)
                QVERIFY(!QFileInfo::exists(ownedPath));
            QCOMPARE(parentPrompts, outcome == "modified" ? 1 : 0);
            if (outcome == "modified")
                QCOMPARE(ExternalEditorFixture::contents(outer, innerName), modifiedBytes);
            QVERIFY(UiInspection::writeReport(window, "location-accepted"));
            return;
        }
        QCOMPARE(panel.GetFolder(), previousFolder);
        QCOMPARE(panel.GetCurrentFolderPrefix(), previousPath);
        QCOMPARE(panel.GetParentFolders().Size(), 2u);
        if (materialized) {
            QVERIFY(QFileInfo::exists(ownedPath));
            QVERIFY(panel.GetParentFolders().Back().OwnedTempLease);
        }
        QTRY_COMPARE(files->rowCount(), entries.size());
        QTRY_COMPARE(files->model()->index(files->currentRow(), 0).data().toString(), selectedName);
        QCOMPARE(files->selectionModel()->selectedRows().size(), 1);
        QTRY_COMPARE(files->verticalScrollBar()->value(), scroll);
        QCOMPARE(address->text(), target);
        QVERIFY(UiInspection::writeReport(window, "location-refused"));
        const int promptsAfterAttempt = prompts;
        const QString output = fixture.filePath("export after refusal");
        window.extractTo(output, true);
        QCOMPARE(ExternalEditorFixture::read(output + '/' + selectedName), selectedBytes);
        QCOMPARE(prompts, promptsAfterAttempt);
        QCOMPARE(prompts, outcome == "cancel" ? 2 : 1);
    }
    void metadataFailureRestoresLiveSource_data() {
        QTest::addColumn<quint32>("failedIndex");
        QTest::newRow("initial") << quint32(0);
        QTest::newRow("deferred") << quint32(257);
    }
    void metadataFailureRestoresLiveSource() {
        QFETCH(quint32, failedIndex);
        NativePropertyFailureRuntime runtime;
        QVERIFY(runtime.prepare(QString::fromUtf8(SEVENZIP_EXTERNAL_RUNTIME),
                                QString::fromUtf8(SEVENZIP_EXTERNAL_FORMAT_FIXTURE)));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString previous = fixture.filePath("previous.7z");
        const QString candidate = fixture.filePath("candidate.fixture7z");
        QVERIFY(runtime.createArchive(previous, 2));
        QVERIFY(runtime.createArchive(candidate, 301));
        ArchiveBackend backend(nullptr, runtime.engine());
        MainWindow window(&backend);
        window.show();
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                QTest::keyClick(message, Qt::Key_Return);
        });
        responder.start(5);
        window.openArchive(previous);
        auto *files = table(window);
        QTRY_COMPARE(files->rowCount(), 2);
        QTRY_VERIFY(files->isEnabled());
        auto &panel = *files->sourcePanel;
        const auto *provider = panel.GetFolder();
        const UString prefix = panel.GetCurrentFolderPrefix();
        files->selectRow(1);
        files->setCurrentCell(1, 0, QItemSelectionModel::NoUpdate);
        const QString selectedName = files->model()->index(1, 0).data().toString();
        QVERIFY(runtime.arm(failedIndex, E_FAIL, 301));
        window.openArchive(candidate);
        QVERIFY(!runtime.receipts().isEmpty());
        QCOMPARE(panel.GetFolder(), provider);
        QCOMPARE(panel.GetCurrentFolderPrefix(), prefix);
        QTRY_COMPARE(files->rowCount(), 2);
        QTRY_VERIFY(files->isEnabled());
        QCOMPARE(files->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(files->model()->index(files->currentRow(), 0).data().toString(), selectedName);
        const QString output = fixture.filePath("export");
        window.extractTo(output, true);
        QCOMPARE(ExternalEditorFixture::read(output + '/' + selectedName), selectedName.toUtf8());
    }
};

QTEST_MAIN(ArchiveNavigationRollbackTests)
#include "archive_navigation_rollback_tests.moc"
