#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/delete_reply.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTreeWidget>

namespace {
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
class CMessageAnswers {
    WidgetDialogFixture &dialogs_;
    QTimer timer_;
    bool cancelQuestions_;

  public:
    QStringList Errors;
    bool CancelledQuestion = false;
    explicit CMessageAnswers(WidgetDialogFixture &dialogs, bool cancelQuestions = false)
        : dialogs_(dialogs), cancelQuestions_(cancelQuestions) {
        dialogs_.connectResponder(timer_, &timer_, [this] {
            auto *widget = dialogs_.activeDialog();
            if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(widget)) {
                CancelledQuestion |= cancelQuestions_;
                ReplyDelete(*confirmation, !cancelQuestions_);
            } else if (auto *box = qobject_cast<QMessageBox *>(widget)) {
                if (box->icon() == QMessageBox::Warning || box->icon() == QMessageBox::Critical)
                    Errors.append(box->text());
                if (box->icon() == QMessageBox::Question && box->button(QMessageBox::Yes)) {
                    if (cancelQuestions_ && box->button(QMessageBox::Cancel)) {
                        CancelledQuestion = true;
                        box->button(QMessageBox::Cancel)->click();
                    } else
                        box->button(QMessageBox::Yes)->click();
                } else
                    box->accept();
            } else if (widget && widget->objectName() == "progressDialog") {
                auto *close = widget->findChild<QPushButton *>("IDCANCEL");
                if (TestProgressDialog::IsCloseButton(close))
                    close->click();
            }
        });
        timer_.start(2);
    }
};
// Stop at the first visible native progress event and record the source phase.
// A small native update can already have entered publication by this event.
class CNativeCancel final : public QObject {
    bool &observed_;
    bool &publication_;
    const QString &archive_;
    const QByteArray &before_;

  public:
    CNativeCancel(bool &observed, bool &publication, const QString &archive,
                  const QByteArray &before)
        : observed_(observed), publication_(publication), archive_(archive), before_(before) {
        qApp->installEventFilter(this);
    }
    ~CNativeCancel() override { qApp->removeEventFilter(this); }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show && !observed_)
            if (auto *progress = dynamic_cast<CProgressDialog *>(object)) {
                observed_ = true;
                progress->Sync.Set_Stopped(true);
                {
                    NWindows::NSynchronization::CCriticalSectionLock lock(progress->Sync._cs);
                    publication_ = QDir::cleanPath(QDir::fromNativeSeparators(ToQString(
                                       progress->Sync._filePath))) == QDir::cleanPath(archive_);
                }
                // MoveArc_Start names the destination before its stop check.
                // After Finish clears that name, the committed bytes identify
                // the later reopen phase instead.
                publication_ = publication_ || readFile(archive_) != before_;
            }
        return false;
    }
};
} // namespace

class CodecCommandsUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QString engine_, module_;
    quint32 version_ = 0;
    bool run(const QString &directory, const QStringList &arguments) {
        QProcess engine;
        engine.setWorkingDirectory(directory);
        engine.start(engine_, arguments);
        return engine.waitForFinished(10000) && engine.exitCode() == 0;
    }

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native command UX remains unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        if (engine_.isEmpty())
            QSKIP("Official engine unavailable; native command UX remains unverified");
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        version_ = backend.codecCatalog()->version();
#endif
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void actualCopyCommandUsesDetectedWriter_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("cancel");
        for (const QString &format : {"7z", "zip", "gzip", "bzip2", "xz", "tar", "wim"})
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(format + (cancel ? " cancel" : " success")))
                    << format << cancel;
    }
    void actualCopyCommandUsesDetectedWriter() {
        QFETCH(QString, format);
        QFETCH(bool, cancel);
        QTemporaryDir fixture(QDir::tempPath() + "/codec UI 空-XXXXXX"), output;
        QVERIFY(fixture.isValid() && output.isValid());
        QString name = QStringLiteral("entry 空 with spaces.bin");
#ifdef Q_OS_WIN
        // Source gzip names are limited by CP_ACP; keep the containing path Unicode.
        if (format == "gzip")
            name = QStringLiteral("entry with spaces.bin");
#endif
        const QString source = fixture.filePath(name);
        const QString archive = fixture.filePath(name + ".unknown");
        QVERIFY(writeFile(source, "original"));
        QVERIFY(run(fixture.path(), {"a", "-t" + format, "--", archive, name}));
        const QByteArray before = readFile(archive);
        QVERIFY(!before.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMessageAnswers messages(dialogs_);
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        QVERIFY(!action(window, "deleteAction")->isEnabled());
        QVERIFY(action(window, "renameAction")->isEnabled());
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        const QByteArray replacement = format == "wim" && cancel
                                           ? QByteArray(1024 * 1024, 'x')
                                           : QByteArray("updated\0binary bytes", 20);
        QVERIFY(writeFile(source, replacement));
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *leftAddress = left->findChild<QLineEdit *>("archivePath");
        auto *leftTable = left->findChild<FileTable *>("archiveTable");
        leftAddress->setText(fixture.path());
        emit leftAddress->returnPressed();
        QTRY_VERIFY(leftTable->isEnabled());
        auto *right = window.findChild<QWidget *>("filePanel1");
        auto *rightTable = right->findChild<FileTable *>("archiveTable");
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        rightTable->setFocus();
        QTRY_VERIFY(rightTable->hasFocus());
        window.openArchive(archive);
        QTRY_COMPARE(rightTable->rowCount(), 1);
        QTRY_VERIFY(rightTable->isEnabled());
        QTRY_VERIFY(action(window, "testArchiveAction")->isEnabled());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QApplication::processEvents();
        QTRY_VERIFY(leftTable->isEnabled());
        leftTable->setFocus();
        QTRY_VERIFY(leftTable->hasFocus());
        int sourceRow = -1;
        for (int row = 0; row < leftTable->rowCount(); ++row)
            if (leftTable->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name)
                sourceRow = row;
        QVERIFY(sourceRow >= 0);
        leftTable->selectRow(sourceRow);
        bool submitted = false;
        dialogs_.respond(window, [&](QDialog &active) {
            auto *dialog = qobject_cast<CopyDialog *>(&active);
            QVERIFY(dialog);
            const auto closeOnFailure = qScopeGuard([&] {
                if (!submitted)
                    dialog->reject();
            });
            auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
            QVERIFY(destination);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(destination->text())),
                     QDir::cleanPath(archive));
            dialog->resize(cancel ? QSize(480, 320) : QSize(640, 400));
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*destination, *dialog));
            QVERIFY(UiInspection::writeReport(
                *dialog, QString("codec-copy-%1-%2").arg(format).arg(dialog->width())));
            QVERIFY(UiInspection::fullyVisible(
                *dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok), *dialog));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            submitted = dialog->result() == QDialog::Accepted;
        });
        bool observedStart = false;
        bool publicationAtCancel = false;
        std::unique_ptr<CNativeCancel> nativeCancel;
        if (cancel)
            nativeCancel = std::make_unique<CNativeCancel>(observedStart, publicationAtCancel,
                                                           archive, before);
        action(window, "copyAction")->trigger();
        nativeCancel.reset();
        QVERIFY(submitted);
        if (cancel)
            QVERIFY(observedStart);
        QVERIFY2(messages.Errors.isEmpty(), qPrintable(messages.Errors.join('\n')));
        QVERIFY(!backend.busy());
        if (cancel)
            qInfo() << "Native cancel phase:" << (publicationAtCancel ? "publication" : "writer");
        if (cancel && (!publicationAtCancel || readFile(archive) == before))
            QCOMPARE(readFile(archive), before);
        else {
            QVERIFY(run(fixture.path(), {"x", "-y", "-o" + output.path(), "--", archive}));
            QCOMPARE(readFile(output.filePath(name)), replacement);
        }
        QCOMPARE(readFile(source), replacement);
        QVERIFY(
            QDir(fixture.path()).entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void singleStreamArchiveHasNoTrashCommand_data() {
        QTest::addColumn<QString>("format");
        for (const QString &format : {"bzip2", "xz"})
            QTest::newRow(qPrintable(format)) << format;
    }
    void singleStreamArchiveHasNoTrashCommand() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("stream 空 with spaces.bin");
        const QString source = fixture.filePath(name);
        const QString archive = fixture.filePath(name + ".unknown");
        QVERIFY(writeFile(source, "stream payload"));
        QVERIFY(run(fixture.path(), {"a", "-t" + format, "--", archive, name}));
        const QByteArray before = readFile(archive);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        QVERIFY(!action(window, "deleteAction")->isEnabled());
        QCOMPARE(readFile(archive), before);
        QCOMPARE(table(window)->rowCount(), 1);
        QVERIFY(action(window, "testArchiveAction")->isEnabled());
        QVERIFY(!backend.busy());
    }
    void unavailableCatalogPreservesFilesystemNavigation_data() {
        QTest::addColumn<bool>("mismatch");
        QTest::newRow("missing") << false;
        QTest::newRow("version mismatch") << true;
    }
    void unavailableCatalogPreservesFilesystemNavigation() {
        QFETCH(bool, mismatch);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(writeFile(fixture.filePath("payload.bin"), "fallback"));
        const QString archive = fixture.filePath("content.unknown");
        QVERIFY(run(fixture.path(), {"a", "-tzip", "--", archive, "payload.bin"}));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(mismatch ? module_ : fixture.filePath("missing module"),
                                          mismatch ? version_ + 1 : version_),
                 E_FAIL);
        MainWindow window(&backend);
        CMessageAnswers messages(dialogs_);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        const QString initialDirectory = address->text();
        QStringList initialRows;
        for (int row = 0; row < table(window)->rowCount(); ++row)
            initialRows.append(table(window)->model()->index(row, 0).data().toString());
        window.openArchive(archive);
        QTRY_VERIFY(!messages.Errors.isEmpty());
        QCOMPARE(address->text(), initialDirectory);
        QStringList rows;
        for (int row = 0; row < table(window)->rowCount(); ++row)
            rows.append(table(window)->model()->index(row, 0).data().toString());
        QCOMPARE(rows, initialRows);
        QVERIFY(table(window)->isEnabled());
        QVERIFY(action(window, "refreshAction")->isEnabled());
    }
    void openAsArchiveDetectsZipWithoutKnownExtension() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("payload.bin");
        const QString archive = fixture.filePath(QStringLiteral("архив 空.unknown"));
        QVERIFY(writeFile(source, "inside fixture"));
        QVERIFY(run(fixture.path(), {"a", "-tzip", "--", archive, "payload.bin"}));
        const QByteArray bytes = readFile(archive);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CMessageAnswers messages(dialogs_);
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, QFileInfo(archive).fileName()) >= 0);
        table(window)->selectRow(filesystemRow(window, QFileInfo(archive).fileName()));
        QTRY_VERIFY(action(window, "openInsideAction")->isEnabled());
        action(window, "openInsideAction")->trigger();
        QTRY_VERIFY(action(window, "testArchiveAction")->isEnabled());
        QCOMPARE(table(window)->rowCount(), 1);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QString("payload.bin"));
        QVERIFY(action(window, "toolbarAddAction")->isEnabled());
        QVERIFY2(messages.Errors.isEmpty(), qPrintable(messages.Errors.join('\n')));
        QCOMPARE(readFile(archive), bytes);
    }
};
QTEST_MAIN(CodecCommandsUiTests)
#include "codec_commands_ui_tests.moc"
