#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/copy/alias.h"
#include "filemanager/support/window_state_support.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"
#include <QLineEdit>

class AltStreamsArchiveUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
    static int nativeRow(MainWindow &window, const QString &name) {
        auto *view = table(window);
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()
                    ->index(row, 0)
                    .data(Qt::UserRole + 3)
                    .toString()
                    .normalized(QString::NormalizationForm_C) == name)
                return row;
        return -1;
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; archive streams UI remains unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module_, 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
#endif
    }
    void sourceArchiveStreamsCommand_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("streamCount");
        QTest::newRow("en") << QStringLiteral("-") << 1;
        QTest::newRow("ru") << QStringLiteral("ru") << 1;
#ifdef Q_OS_WIN
        QTest::newRow("deferred") << QStringLiteral("-") << 257;
#endif
    }
    void sourceTreeWriterPreservesOrdinaryPayload() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("исходный файл δ.bin");
        QFile file(fixture.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("tree writer payload"), qint64(19));
        file.close();
        const QString archive = fixture.filePath("native tree.wim");
        QProcess engine;
        engine.setWorkingDirectory(fixture.path());
        engine.start(engine_, {"a", "-twim", "--", archive, name});
        QVERIFY(engine.waitForFinished(10000));
        QCOMPARE(engine.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(nativeRow(window, name) >= 0);
        table(window)->selectRow(nativeRow(window, name));
        table(window)->setCurrentCell(nativeRow(window, name), 0);
        QVERIFY(action(window, "renameAction")->isEnabled());
        action(window, "renameAction")->trigger();
        auto *editor = table(window)->findChild<QLineEdit *>();
        QVERIFY(editor);
        const QString renamed = QStringLiteral("новое имя δ.bin");
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(nativeRow(window, renamed) >= 0);
        QVERIFY(nativeRow(window, name) < 0);
        const QString entry = table(window)
                                  ->model()
                                  ->index(nativeRow(window, renamed), 0)
                                  .data(Qt::UserRole)
                                  .toString();
        engine.start(engine_, {"x", "-so", "-spd", "-ssc", "--", archive, entry});
        QVERIFY(engine.waitForFinished(10000));
        QCOMPARE(engine.exitCode(), 0);
        QCOMPARE(engine.readAllStandardOutput(), QByteArray("tree writer payload"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("tree writer payload"));
    }
    void sourceArchiveStreamsCommand() {
        QFETCH(QString, language);
        QFETCH(int, streamCount);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        const QString name = QStringLiteral("файл δ with space.bin");
        const QString streamName = QStringLiteral("поток δ");
        QFile base(temp.filePath(name));
        QVERIFY(base.open(QIODevice::WriteOnly));
        QCOMPARE(base.write("main bytes"), qint64(10));
        base.close();
#ifdef Q_OS_WIN
        QFile stream(base.fileName() + ':' + streamName);
        QVERIFY(stream.open(QIODevice::WriteOnly));
        QCOMPARE(stream.write("archive stream bytes"), qint64(20));
        stream.close();
        for (int i = 1; i < streamCount; ++i) {
            QFile extra(base.fileName() + QStringLiteral(":extra-%1").arg(i));
            QVERIFY(extra.open(QIODevice::WriteOnly));
            QCOMPARE(extra.write("extra stream"), qint64(12));
        }
#endif
        const QString archive = temp.filePath("archive streams.wim");
        QProcess creator;
        creator.setWorkingDirectory(temp.path());
#ifdef Q_OS_WIN
        creator.start(engine_, {"a", "-twim", "-sns", "--", archive, name});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
#else
        // WIM stores real ADS independently of the host filesystem capability.
        QVERIFY(QFile::copy(QString::fromUtf8(ALT_STREAMS_FIXTURE), archive));
#endif
        QFile selection(temp.filePath("selection.txt"));
        QVERIFY(selection.open(QIODevice::WriteOnly));
        const QByteArray selectedName = (name + ':' + streamName + '\n').toUtf8();
        QCOMPARE(selection.write(selectedName), qint64(selectedName.size()));
        selection.close();
        creator.start(engine_, {"x", "-so", "-sns", "-spd", "-ssc", "-twim", "-scsUTF-8",
                                "-i@" + selection.fileName(), "--", archive});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QCOMPARE(creator.readAllStandardOutput(), QByteArray("archive stream bytes"));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_VERIFY(nativeRow(window, name) >= 0);
        auto *streams = action(window, "altStreamsAction");
        table(window)->selectRow(nativeRow(window, name));
        QTRY_VERIFY(streams->isEnabled());
        streams->trigger();
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*table(window), window));
            QVERIFY(UiInspection::fullyVisible(
                *PanelKeys::navigationFor(window.findChild<QLineEdit *>("archivePath")), window));
            QVERIFY(UiInspection::writeReport(window, QStringLiteral("archive-alt-streams-%1-%2x%3")
                                                          .arg(language)
                                                          .arg(size.width())
                                                          .arg(size.height())));
        }
        QTRY_VERIFY(nativeRow(window, streamName) >= 0);
        QTRY_VERIFY(table(window)->isEnabled());
        QVERIFY(table(window)->currentIndex().isValid());
        QCOMPARE(table(window)->model()->index(nativeRow(window, streamName), 0).data().toString(),
                 streamName);
        QVERIFY(window.findChild<QLineEdit *>("archivePath")->text().contains(archive));
        table(window)->selectRow(nativeRow(window, streamName));
        const QString output = temp.filePath("copied streams");
        QVERIFY(QDir().mkpath(output));
        bool dialogSeen = false;
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
                return;
            }
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            if (!dialog || !dialog->isVisible())
                return;
            dialogSeen = true;
            dialog->findChild<QLineEdit *>("copyDestination")->setText(output + '/');
            dialog->accept();
        });
        responder.start(1);
        QVERIFY(action(window, "copyAction")->isEnabled());
        action(window, "copyAction")->trigger();
        responder.stop();
        QVERIFY(dialogSeen);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QFile copied(QDir(output).filePath(streamName));
        QStringList outputNames;
        for (const QString &file : QDir(output).entryList(QDir::Files | QDir::Hidden))
            outputNames << QString::fromLatin1(file.toUtf8().toHex());
        QTRY_VERIFY2(QFileInfo::exists(copied.fileName()),
                     qPrintable(outputNames.join(',') + " expected " +
                                QString::fromLatin1(streamName.toUtf8().toHex()) + " | " +
                                window.statusBar()->currentMessage()));
        QTRY_COMPARE(QFileInfo(copied.fileName()).size(), qint64(20));
        QVERIFY2(copied.open(QIODevice::ReadOnly), qPrintable(copied.errorString()));
        QCOMPARE(copied.readAll(), QByteArray("archive stream bytes"));
        QTRY_VERIFY(table(window)->isEnabled());
        QTRY_VERIFY(nativeRow(window, streamName) >= 0);
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        table(window)->selectRow(nativeRow(window, streamName));
        table(window)->setCurrentCell(nativeRow(window, streamName), 0);
        auto *rename = action(window, "renameAction");
        QVERIFY(rename->isEnabled());
        rename->trigger();
        auto *editor = table(window)->findChild<QLineEdit *>();
        QVERIFY(editor);
        const QString renamed = QStringLiteral("новое имя потока δ");
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(nativeRow(window, renamed) >= 0);
        QTRY_VERIFY(table(window)->isEnabled());
        QVERIFY(nativeRow(window, streamName) < 0);
        table(window)->selectRow(nativeRow(window, renamed));
        table(window)->setCurrentCell(nativeRow(window, renamed), 0);
        auto *remove = action(window, "deleteAction");
        QVERIFY(remove->isEnabled());
        for (QMessageBox::StandardButton answer : {QMessageBox::Cancel, QMessageBox::Yes}) {
            bool asked = false;
            QStringList mutationErrors;
            QTimer confirmation;
            connect(&confirmation, &QTimer::timeout, &window, [&] {
                if (auto *box =
                        dynamic_cast<QtDeleteConfirmation *>(QApplication::activeModalWidget())) {
                    asked = true;
                    QCOMPARE(box->windowTitle(),
                             QCoreApplication::translate("DeleteRedesign", "Delete from archive?"));
                    ReplyDelete(*box, answer == QMessageBox::Yes);
                } else if (auto *box =
                               qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    mutationErrors << box->text();
                    box->accept();
                }
            });
            confirmation.start(1);
            remove->trigger();
            confirmation.stop();
            QVERIFY(asked);
            QVERIFY2(mutationErrors.isEmpty(), qPrintable(mutationErrors.join('\n')));
            if (answer == QMessageBox::Cancel)
                QVERIFY(nativeRow(window, renamed) >= 0);
            else {
                QVERIFY(nativeRow(window, renamed) < 0);
                // Original Restore FolderItem falls back to the ordinary file
                // owner when its final ADS directory disappears.
                if (streamCount > 1) {
                    QTRY_VERIFY(table(window)->isEnabled());
                    QTRY_VERIFY(nativeRow(window, QStringLiteral("extra-1")) >= 0);
                    action(window, "parentFolderAction")->trigger();
                }
                QTRY_VERIFY(nativeRow(window, name) >= 0);
            }
        }
        creator.start(engine_, {"t", "--", archive});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QTRY_VERIFY(nativeRow(window, name) >= 0);
        const QString mainEntry =
            table(window)->model()->index(nativeRow(window, name), 0).data(Qt::UserRole).toString();
        QVERIFY(selection.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray mainName = (mainEntry + '\n').toUtf8();
        QCOMPARE(selection.write(mainName), qint64(mainName.size()));
        selection.close();
        creator.start(engine_, {"x", "-so", "-sns-", "-spd", "-ssc", "-scsUTF-8",
                                "-i@" + selection.fileName(), "--", archive});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QCOMPARE(creator.readAllStandardOutput(), QByteArray("main bytes"));
        QVERIFY(base.open(QIODevice::ReadOnly));
        QCOMPARE(base.readAll(), QByteArray("main bytes"));
    }
};
QTEST_MAIN(AltStreamsArchiveUiTests)
#include "alt_streams_archive_ui_tests.moc"
