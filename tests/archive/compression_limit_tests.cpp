#include "7zip/UI/FileManager/resourceGui.h"
#include "archive/backend/archivebackend.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "support/archive_expert.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <algorithm>

class CompressionLimitTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory_;
    QByteArray previousSettings_;

  private slots:
    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settingsDirectory_.filePath("limit.ini").toUtf8());
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void customLimitRestorationDoesNotDuplicateChoices() {
        const auto choices = NCompressDialog::SetMemUseCombo();
        for (QString custom : {QString("37%"), QString("110%"), QString("320M"), QString("128M")}) {
            const auto restored = NCompressDialog::SetMemUseCombo(custom);
            int occurrences = 0;
            for (const auto &item : restored)
                occurrences += item.Spec == custom;
            QCOMPARE(occurrences, 1);
        }
        const auto existing =
            std::find_if(choices.cbegin(), choices.cend(),
                         [](const auto &choice) { return !choice.Spec.isEmpty(); });
        QVERIFY(existing != choices.cend());
        QCOMPARE(NCompressDialog::SetMemUseCombo(existing->Spec).size(), choices.size());
    }
    void automaticThreadsRespectLimit() {
        NCompressDialog::CSystemInfo system;
        system.NumCPUs = system.NumHardwareThreads = 16;
        system.RamSizeDefined = true;
        system.RamSize = system.RamSizeReduced = 1 << 30;
        system.RamUsageAuto = Calc_From_Val_Percents(system.RamSizeReduced, 80);
        NCompression::CMemUse limit;
        limit.Parse("64M");
        QCOMPARE(NCompressDialog::Get_MemUse_Bytes(limit, system), UInt64(64 << 20));
        auto threads = NCompressDialog::SetNumThreads2(
            QStringLiteral("7z"), NCompressDialog::kLZMA2, 5, 1 << 20, UInt32(-1), system, limit);
        QCOMPARE(threads.AutoNumThreads, 4u);
        limit.Parse("50%");
        QCOMPARE(NCompressDialog::Get_MemUse_Bytes(limit, system), UInt64(512 << 20));
        limit.Clear();
        QCOMPARE(NCompressDialog::Get_MemUse_Bytes(limit, system), system.RamUsageAuto);
        system.RamSizeDefined = false;
        limit.Parse("64M");
        threads = NCompressDialog::SetNumThreads2(QStringLiteral("7z"), NCompressDialog::kLZMA2, 5,
                                                  1 << 20, UInt32(-1), system, limit);
        QCOMPARE(threads.AutoNumThreads, 16u);
    }
    void dialogValidationAndLayout() {
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("contents"), qint64(8));
        file.close();
        CreateArchiveDialog dialog(dir.path(), {source});
        auto *limit = dialog.findChild<QComboBox *>("compressionMemoryLimit");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        QVERIFY(limit && method);
        QVERIFY(!limit->isEditable());
        method->setCurrentText("PPMd");
        limit->setCurrentIndex(limit->findData("256M"));
        QCOMPARE(dialog.compressionOptions().MemUsage.GetBytes(0), UInt64(256 << 20));
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("zip");
        QCOMPARE(limit->currentData().toString(), QString());
        format->setCurrentText("7z");
        QCOMPARE(limit->currentData().toString(), QString("256M"));
        format->setCurrentText("zip");
        limit->setCurrentIndex(limit->findData("50%"));
        QVERIFY(dialog.compressionOptions().MemUsage.IsPercent);
        QCOMPARE(dialog.compressionOptions().MemUsage.Val, UInt64(50));
        limit->setCurrentIndex(0);
        QVERIFY(!dialog.compressionOptions().MemUsage.IsDefined);
        // Inject a parsed zero-byte choice to exercise OnOK independent of physical RAM.
        limit->addItem("0 B", "0b");
        limit->setCurrentIndex(limit->count() - 1);
        ExpandArchiveSections(dialog);
        dialog.show();
        bool rejectedForMemory = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                rejectedForMemory = message->text().contains(
                    QString::fromWCharArray(LangString(IDS_MEM_USAGE_LIMIT_SET_BY_7ZIP).Ptr()));
                message->accept();
            } else if (auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                       editor && editor->objectName() == "archiveExpertDialog") {
                responder.stop();
                editor->reject();
            }
        });
        responder.start(1);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        responder.stop();
        QVERIFY(rejectedForMemory);
        QCOMPARE(dialog.result(), 0);
        const auto retainedLimit = dialog.compressionOptions().MemUsage;
        QVERIFY(retainedLimit.IsDefined);
        QVERIFY(!retainedLimit.IsPercent);
        QCOMPARE(retainedLimit.Val, UInt64(0));
        limit->setCurrentIndex(limit->findData("256M"));
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            for (QSize size : {QSize(760, 720), QSize(640, 480)}) {
                editor.resize(size);
                editor.findChild<QScrollArea *>("dialogContentScroll")
                    ->ensureWidgetVisible(limit, 0, 80);
                QTRY_VERIFY(UiInspection::fullyVisible(*limit, editor));
                QVERIFY(UiInspection::writeReport(editor,
                                                  QString("memory-limit-%1").arg(size.width())));
            }
        }));
        limit->setCurrentIndex(0);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }
    void cancelAfterMemoryRejectionPreservesProfile() {
        QTemporaryDir dir;
        QFile source(dir.filePath("source.txt"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("contents"), qint64(8));
        source.close();
        QFile settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QByteArray before;
        if (settings.exists()) {
            QVERIFY(settings.open(QIODevice::ReadOnly));
            before = settings.readAll();
            settings.close();
        }
        const bool existed = settings.exists();
        CreateArchiveDialog dialog(dir.path(), {source.fileName()});
        auto *limit = dialog.findChild<QComboBox *>("compressionMemoryLimit");
        QVERIFY(limit);
        limit->addItem("0 B", "0b");
        limit->setCurrentIndex(limit->count() - 1);
        ExpandArchiveSections(dialog);
        dialog.show();
        bool rejected = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                rejected = message->text().contains(
                    QString::fromWCharArray(LangString(IDS_MEM_USAGE_LIMIT_SET_BY_7ZIP).Ptr()));
                message->accept();
            } else if (auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                       editor && editor->objectName() == "archiveExpertDialog") {
                responder.stop();
                editor->reject();
            }
        });
        responder.start(1);
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        buttons->button(QDialogButtonBox::Ok)->click();
        responder.stop();
        QVERIFY(rejected);
        QVERIFY(dialog.isVisible());
        buttons->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(settings.exists(), existed);
        if (existed) {
            QVERIFY(settings.open(QIODevice::ReadOnly));
            QCOMPARE(settings.readAll(), before);
        }
    }
    void operations_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("spec");
        QTest::newRow("7z-bytes") << QString("7z") << QString("256M");
        QTest::newRow("7z-percent") << QString("7z") << QString("80%");
        QTest::newRow("zip-bytes") << QString("zip") << QString("256M");
        QTest::newRow("zip-percent") << QString("zip") << QString("80%");
    }
    void operations() {
        QFETCH(QString, format);
        QFETCH(QString, spec);
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(source);
        const QByteArray bytes(65536, 'x');
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        ArchiveBackend backend;
        CompressionOptions options;
        options.Method = format == "zip" ? "LZMA" : "PPMd";
        options.Dict64 = 1 << 20;
        options.MemUsage.Parse(spec);
        const QString target = dir.filePath("limit." + format);
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source}, {}, false, options),
                 S_OK);
        done.clear();
        QProcess process;
        process.start("7z", {"t", "--", target});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 0);
        process.start("7z", {"x", "-o" + dir.filePath("out"), "--", target});
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 0);
        QFile restored(dir.filePath("out/" + QFileInfo(source).fileName()));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), bytes);
        options.MemUsage.Parse("1M");
        QVERIFY(SourceCreationFixture::Create(backend, dir.filePath("rejected." + format), {source},
                                              {}, false, options) != S_OK);
        QVERIFY(!QFileInfo::exists(dir.filePath("rejected." + format)));
        done.clear();
        options.MemUsage.Parse(spec);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(8 << 20));
        file.close();
        const QString cancelled = dir.filePath("cancelled." + format);
        QCOMPARE(
            SourceCreationFixture::Create(backend, cancelled, {source}, {}, false, options, true),
            E_ABORT);
        QVERIFY(!QFileInfo::exists(cancelled));
        QVERIFY(QFileInfo::exists(target));
        QVERIFY(QDir(dir.path()).entryList({"*.stage*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
};
QTEST_MAIN(CompressionLimitTests)
#include "compression_limit_tests.moc"
