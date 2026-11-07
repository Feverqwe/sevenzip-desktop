#include "archive/backend/archivebackend.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivebatchextract.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include "settings/RegistryUtils.h"
#include "support/progress_dialog.h"
#include "ui/support/layout_inspection.h"
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {
int Row(FileTable &table, const QString &name) {
    for (int i = 0; i < table.rowCount(); ++i)
        if (table.model()->index(i, 0).isValid() &&
            table.model()->index(i, 0).data().toString() == name)
            return i;
    return -1;
}
} // namespace

class ArchiveRecoveryUiTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
    QTemporaryDir settings_;
    QByteArray previousSettings_;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; recovery UI unverified");
#else
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
#endif
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        QVERIFY(settings_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
    }
    void cleanupTestCase() { qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_); }
    void refusesTraversalInRecoveredCatalogue() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath("payload.txt"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("payload"), qint64(7));
        input.close();
        const QString path = fixture.filePath("unsafe.zip");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-tzip", "-mx=0", "--", path, "payload.txt"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QByteArray bytes = file.readAll();
        file.close();
        const auto central = bytes.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central > 0);
        bytes.truncate(central);
        QVERIFY(bytes.contains("payload.txt"));
        bytes.replace("payload.txt", "../evil.txt");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(backend.openNativeArchive(backend.codecCatalog(), path, "zip", nullptr, reader,
                                           nullptr),
                 S_OK);
        QVERIFY(reader);
        QString error;
        QVERIFY(!backend.validateExtraction(fixture.filePath("output"), {}, {}, &error, true, {},
                                            &reader->arc()));
        QVERIFY(!error.isEmpty());
        QSignalSpy complete(&backend, &ArchiveBackend::finished);
        backend.list(path);
        QTRY_COMPARE(complete.size(), 1);
        QVERIFY(complete[0][0].toBool());
        QVERIFY(!backend.validateExtraction(fixture.filePath("process output"), {}, {}, &error));
        QVERIFY(!error.isEmpty());
    }
    void opensReadablePart_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("accept");
        QTest::addColumn<bool>("batch");
        QTest::addColumn<bool>("native");
        for (const QString &language : {"en", "ru"})
            for (bool accept : {false, true})
                for (bool batch : {false, true})
                    for (bool native : {false, true})
                        QTest::newRow(qPrintable(language + (accept ? "-accept" : "-cancel") +
                                                 (batch ? "-batch" : "-open") +
                                                 (native ? "-native" : "-process")))
                            << language << accept << batch << native;
    }
    void opensReadablePart() {
        QFETCH(QString, language);
        QFETCH(bool, accept);
        QFETCH(bool, batch);
        QFETCH(bool, native);
        QtLoadTranslation(language == "en" ? "-" : "ru");
        SaveRegLang(UString(language == "en" ? L"-" : L"ru"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString healthyName = QStringLiteral("читаемый 空 file.txt");
        const QString brokenName = "broken.txt";
        const QByteArray healthy = "readable archive payload", broken = "broken archive payload";
        for (const auto &entry : {qMakePair(healthyName, healthy), qMakePair(brokenName, broken)}) {
            QFile file(fixture.filePath(entry.first));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(entry.second), qint64(entry.second.size()));
        }
        const QString path = fixture.filePath(QStringLiteral("неполный 空 archive.zip"));
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-tzip", "-mx=0", "--", path, healthyName, brokenName});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray bytes = source.readAll();
        source.close();
        const auto central = bytes.indexOf(QByteArray::fromHex("504b0102"));
        QVERIFY(central > 0);
        bytes.truncate(central); // Retain local records, lose the central directory.
        const auto payload = bytes.indexOf(broken);
        QVERIFY(payload >= 0);
        bytes[payload] = '!'; // The second entry must report CRC failure.
        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(source.write(bytes), qint64(bytes.size()));
        source.close();
        ArchiveBackend backend(nullptr, engine_);
        if (native) {
            QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
            QVERIFY(backend.discoverCodecCatalog(module_));
            QTRY_COMPARE(ready.size(), 1);
            QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        }
        MainWindow window(&backend);
        window.openPath(fixture.path());
        window.show();
        int questions = 0, extractionErrors = 0, cancellationResults = 0;
        bool geometry = true, copyingBroken = false;
        QStringList unexpected;
        QPointer<QMessageBox> handled;
        QTimer automation;
        connect(&automation, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *box = qobject_cast<QMessageBox *>(widget);
                    box && box != handled && box->isVisible()) {
                    handled = box;
                    if (box->objectName() == "archiveReadLimitationsWarning") {
                        ++questions;
                        geometry &= box->defaultButton() == box->button(QMessageBox::Cancel);
                        box->layout()->activate();
                        for (auto *label : box->findChildren<QLabel *>())
                            if (label->isVisible())
                                geometry &= UiInspection::fullyVisible(*label, *box) &&
                                            UiInspection::labelFits(*label);
                        box->setProperty("uiInspection_language", language);
                        geometry &=
                            UiInspection::writeReport(*box, "archive-recovery-" + language +
                                                                (native ? "-native" : "-process"));
                        if (accept)
                            box->findChild<QPushButton *>("archiveOpenReadOnlyButton")->click();
                        else
                            QTest::keyClick(box,
                                            language == "en" ? Qt::Key_Return : Qt::Key_Escape);
                    } else {
                        if (copyingBroken)
                            ++extractionErrors;
                        else
                            unexpected.append(box->text());
                        box->reject();
                    }
                    return;
                }
                if (!widget->isVisible())
                    continue;
                QPushButton *close = nullptr;
                QTreeWidget *messages = nullptr;
                if (widget->objectName() == "progressDialog") {
                    close = widget->findChild<QPushButton *>("IDCANCEL");
                    if (!TestProgressDialog::IsCloseButton(close))
                        continue;
                    messages = widget->findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES");
                }
                if (close) {
                    auto *status = widget->findChild<QLabel *>("IDT_PROGRESS_STATUS");
                    const bool cancelled =
                        batch && !accept && status &&
                        status->text() ==
                            QCoreApplication::translate(
                                "ProgressRedesign",
                                "Operation cancelled. Some changes may have completed.");
                    if (cancelled)
                        ++cancellationResults;
                    else if (copyingBroken && messages && messages->topLevelItemCount())
                        ++extractionErrors;
                    else
                        unexpected.append(messages && messages->topLevelItemCount()
                                              ? messages->topLevelItem(0)->text(0)
                                              : "Unexpected operation diagnostics");
                    close->click();
                    return;
                }
            }
        });
        automation.start(10);
        if (batch) {
            const QString output = fixture.filePath("batch output");
            copyingBroken = accept;
            ExtractionOptions options;
            options.overwriteMode = ExtractionOptions::OverwriteMode::SkipExisting;
            const auto result =
                RunQtArchiveBatchExtract(backend, {path}, output, {}, options, &window);
            copyingBroken = false;
            QVERIFY2(questions == 1, qPrintable(QString::number(quint32(result.Result), 16) + ": " +
                                                unexpected.join('\n')));
            QVERIFY(geometry);
            QVERIFY(result.Result != S_OK || !result.CallbackOK);
            QFile copied(output + '/' + healthyName);
            if (accept) {
                QVERIFY(copied.open(QIODevice::ReadOnly));
                QCOMPARE(copied.readAll(), healthy);
                QVERIFY(extractionErrors > 0);
            } else {
                QCOMPARE(result.Result, E_ABORT);
                QCOMPARE(cancellationResults, 1);
                QVERIFY(!copied.exists());
            }
        } else {
            window.openArchive(path);
            QCOMPARE(questions, 1);
            QVERIFY(geometry);
            QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join('\n')));
            auto *badge = window.findChild<QLabel *>("deviceReadOnlyLabel");
            QVERIFY(badge);
            QCOMPARE(badge->isVisible(), accept);
            if (accept) {
                auto *table = window.findChild<FileTable *>("archiveTable");
                QVERIFY(table);
                QTRY_VERIFY(Row(*table, healthyName) >= 0);
                table->selectRow(Row(*table, healthyName));
                for (const char *name : {"deleteAction", "renameAction", "moveAction"})
                    QVERIFY(!window.findChild<QAction *>(name)->isEnabled());
                for (QSize size : {QSize(980, 620), QSize(640, 400)}) {
                    window.resize(size);
                    QCoreApplication::processEvents();
                    QVERIFY(UiInspection::fullyVisible(*badge, window));
                    QVERIFY(UiInspection::labelFits(*badge));
                    window.setProperty("uiInspection_language", language);
                    QVERIFY(UiInspection::writeReport(window, "archive-recovery-main-" + language +
                                                                  '-' +
                                                                  QString::number(size.width())));
                }
                const QString output = fixture.filePath("copied");
                copyingBroken = !native; // The process reports the archive's header error too.
                window.extractTo(output, true);
                copyingBroken = false;
                QFile copied(output + '/' + healthyName);
                QVERIFY(copied.open(QIODevice::ReadOnly));
                QCOMPARE(copied.readAll(), healthy);
                table->selectRow(Row(*table, brokenName));
                copyingBroken = true;
                window.extractTo(output, true);
                copyingBroken = false;
                QVERIFY(extractionErrors > 0);
                QFile unverified(output + '/' + brokenName);
                QVERIFY(unverified.open(QIODevice::ReadOnly));
                QCOMPARE(unverified.readAll(), QByteArray("!") + broken.mid(1));
                const QString allOutput = fixture.filePath("all copied");
                copyingBroken = true;
                const int previousErrors = extractionErrors;
                window.extractTo(allOutput, false);
                copyingBroken = false;
                QVERIFY(extractionErrors > previousErrors);
                QFile allHealthy(allOutput + '/' + healthyName);
                QVERIFY(allHealthy.open(QIODevice::ReadOnly));
                QCOMPARE(allHealthy.readAll(), healthy);
                QVERIFY(badge->isVisible());
                QCOMPARE(questions, 1);
                window.openPath(fixture.path());
                window.openArchive(path);
                QCOMPARE(questions, 2);
            }
        }
        automation.stop();
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join('\n')));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), bytes);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ArchiveRecoveryUiTests)
#include "archive_recovery_ui_tests.moc"
