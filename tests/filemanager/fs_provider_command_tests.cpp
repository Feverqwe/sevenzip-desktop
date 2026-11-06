#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"
#include <QLineEdit>

class FsProviderCommandTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
                return i;
        return -1;
    }
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = panel->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(panel->findChild<FileTable *>("archiveTable"));
    }
    static void inspectPanels(MainWindow &window, FileTable &source, FileTable &peer,
                              const QString &tag) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(source, window));
            QVERIFY(UiInspection::fullyVisible(peer, window));
            for (auto *address : window.findChildren<QLineEdit *>("archivePath"))
                QVERIFY(UiInspection::fullyVisible(*address, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("fs-provider-%1-%2x%3").arg(tag).arg(size.width()).arg(size.height())));
        }
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void caseOnlyRename_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<bool>("directory");
        for (const QString &language : {QStringLiteral("-"), QStringLiteral("ru")})
            for (unsigned source : {0u, 1u})
                for (bool directory : {false, true})
                    QTest::newRow(qPrintable(
                        QString("%1-pane%2-%3").arg(language).arg(source).arg(directory)))
                        << language << source << directory;
    }
    void caseOnlyRename() {
        QFETCH(QString, language);
        QFETCH(unsigned, sourceIndex);
        QFETCH(bool, directory);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(QDir().mkpath(temp.filePath("source")));
        QVERIFY(QDir().mkpath(temp.filePath("peer")));
        const QString oldName = QStringLiteral("MiXeD файл δ");
        const QString newName = QStringLiteral("mixed файл δ");
        const QString oldPath = temp.filePath("source/" + oldName);
        if (directory)
            QVERIFY(QDir().mkdir(oldPath));
        write(oldPath + (directory ? "/child" : ""), "protected bytes");
        write(temp.filePath("peer/control"), "peer bytes");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - sourceIndex, temp.filePath("peer"));
        auto *source = openPanel(window, sourceIndex, temp.filePath("source"));
        QTRY_VERIFY(row(*source, oldName) >= 0);
        source->setCurrentCell(row(*source, oldName), 0);
        source->selectRow(row(*source, oldName));
        source->setFocus();
        auto *sourceFolder = source->sourcePanel->GetFolder();
        auto *peerFolder = peer->sourcePanel->GetFolder();
        // Escape must preserve the source spelling and selection before the accepted F2.
        QTest::keyClick(source, Qt::Key_F2);
        auto *editor = source->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(newName);
        QTest::keyClick(editor, Qt::Key_Escape);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
        QTRY_VERIFY(row(*source, oldName) >= 0);
        source->setFocus();
        QTest::keyClick(source, Qt::Key_F2);
        editor = source->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(newName);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(row(*source, newName) >= 0);
        const QStringList names =
            QDir(temp.filePath("source")).entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        QCOMPARE(names, QStringList{newName});
        QCOMPARE(read(temp.filePath("source/" + newName) + (directory ? "/child" : "")),
                 QByteArray("protected bytes"));
        QCOMPARE(source->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(source->currentRow(), row(*source, newName));
        QCOMPARE(source->sourcePanel->GetFolder(), sourceFolder);
        QCOMPARE(peer->sourcePanel->GetFolder(), peerFolder);
        QCOMPARE(read(temp.filePath("peer/control")), QByteArray("peer bytes"));
        QCOMPARE(peer->rowCount(), 1);
        inspectPanels(window, *source, *peer, QString("rename-%1").arg(QTest::currentDataTag()));
    }
    void copyToPeerStreams_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<int>("outcome");
        for (const QString &language : {QStringLiteral("-"), QStringLiteral("ru")})
            for (unsigned source : {0u, 1u})
                for (int outcome : {0, 1, 2, 3})
                    QTest::newRow(qPrintable(
                        QString("%1-pane%2-outcome%3").arg(language).arg(source).arg(outcome)))
                        << language << source << outcome;
    }
    void copyToPeerStreams() {
#ifndef Q_OS_WIN
        QSKIP("The source alternate-stream destination requires NTFS.");
#else
        QFETCH(QString, language);
        QFETCH(unsigned, sourceIndex);
        QFETCH(int, outcome);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(QDir().mkdir(temp.filePath("source")));
        const QString name = QStringLiteral("файл δ with space.bin");
        const QString sourcePath = temp.filePath("source/" + name);
        const QString basePath = temp.filePath(QStringLiteral("основной файл.bin"));
        const QString targetPath = basePath + ':' + name;
        const QByteArray payload(150000, 'p');
        write(sourcePath, payload);
        write(basePath, "protected base");
        write(basePath + ":control", "protected stream");
        if (outcome == 3)
            write(targetPath, "existing stream");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - sourceIndex, basePath + ':');
        QTRY_VERIFY(row(*peer, "control") >= 0);
        auto *source = openPanel(window, sourceIndex, temp.filePath("source"));
        QTRY_VERIFY(row(*source, name) >= 0);
        source->setCurrentCell(row(*source, name), 0);
        source->selectRow(row(*source, name));
        source->setFocus();
        auto *sourceFolder = source->sourcePanel->GetFolder();
        auto *peerFolder = peer->sourcePanel->GetFolder();
        bool dialogSeen = false;
        bool overwriteSeen = false;
        QString destination;
        QString errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (dialog && dialog->isVisible() && !dialogSeen) {
                dialogSeen = true;
                destination = dialog->findChild<QLineEdit *>("copyDestination")->text();
                for (const QSize size : {QSize(440, 250), QSize(340, 220)}) {
                    dialog->resize(size);
                    dialog->layout()->activate();
                    for (const char *control : {"copyDestination", "copyBrowseButton",
                                                "copySourceSummary", "copyButtons"})
                        QVERIFY(UiInspection::fullyVisible(*dialog->findChild<QWidget *>(control),
                                                           *dialog));
                    QVERIFY(UiInspection::writeReport(*dialog, QString("fs-provider-copy-%1-%2x%3")
                                                                   .arg(QTest::currentDataTag())
                                                                   .arg(size.width())
                                                                   .arg(size.height())));
                }
                if (outcome == 2)
                    dialog->reject();
                else
                    dialog->accept();
            }
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (widget->objectName() == "overwriteDialog" && widget->isVisible()) {
                    overwriteSeen = true;
                    widget->findChild<QPushButton *>("overwriteNo")->click();
                }
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errors += box->text();
                    box->accept();
                }
            }
        });
        responder.start(1);
        QTest::keyClick(source, outcome == 1 ? Qt::Key_F6 : Qt::Key_F5);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        responder.stop();
        QVERIFY(dialogSeen);
        QCOMPARE(QDir::fromNativeSeparators(destination),
                 QDir::fromNativeSeparators(basePath + ':'));
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QCOMPARE(overwriteSeen, outcome == 3);
        QCOMPARE(read(basePath), QByteArray("protected base"));
        QCOMPARE(read(basePath + ":control"), QByteArray("protected stream"));
        QCOMPARE(QFileInfo::exists(sourcePath), outcome != 1);
        if (outcome < 2) {
            QCOMPARE(read(targetPath), payload);
            QTRY_VERIFY(row(*peer, name) >= 0);
        } else if (outcome == 3) {
            QCOMPARE(read(targetPath), QByteArray("existing stream"));
        } else {
            QVERIFY(!QFileInfo::exists(targetPath));
        }
        QCOMPARE(source->sourcePanel->GetFolder(), sourceFolder);
        QCOMPARE(peer->sourcePanel->GetFolder(), peerFolder);
        QCOMPARE(source->rowCount(), outcome == 1 ? 0 : 1);
        inspectPanels(window, *source, *peer, QString("streams-%1").arg(QTest::currentDataTag()));
#endif
    }
};

QTEST_MAIN(FsProviderCommandTests)
#include "fs_provider_command_tests.moc"
