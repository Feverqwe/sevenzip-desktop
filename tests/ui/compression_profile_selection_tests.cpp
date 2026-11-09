#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/archive_expert.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class CompressionProfileSelectionTests final : public QObject {
    Q_OBJECT
  private slots:
    void retiredZipMethodFallsBackWithoutChangingSavedSettings() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto previous = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previous);
        });
        const QString settings = fixture.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        NCompression::CInfo registry;
        registry.ArcType = "zip";
        auto &profile = registry.Formats.AddNew();
        profile.FormatID = "zip";
        profile.Method = "PPMd";
        registry.Save(settings);
        QFile file(settings);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray before = file.readAll();
        file.close();
        CreateArchiveDialog dialog(fixture.path());
        QCOMPARE(dialog.findChild<QComboBox *>("archiveFormat")->currentText(), QString("zip"));
        const auto options = dialog.compressionOptions();
        QVERIFY(options.Method != L"PPMd");
        CObjectVector<CProperty> properties;
        QString error;
        QVERIFY2(
            ArchiveBackend::compressionProperties("zip", {}, false, options, &properties, &error),
            qPrintable(error));
        dialog.reject();
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
    }
    void sourceFormatComparison() {
        NCompression::CInfo registry;
        registry.Formats.AddNew().FormatID = "K";
        QCOMPARE(CCompressDialog::FindRegistryFormat(registry, "k"), 0);
        QCOMPARE(CCompressDialog::FindRegistryFormat(registry, QString(QChar(0x212a))), -1);
    }
    void restoredValues_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("method");
        QTest::addColumn<UInt32>("dictionary");
        QTest::addColumn<UInt64>("expectedDictionary");
        QTest::addColumn<UInt32>("order");
        QTest::addColumn<UInt32>("expectedOrder");
        QTest::addColumn<UInt32>("block");
        QTest::addColumn<UInt32>("expectedBlock");
        QTest::addColumn<QString>("language");
        for (const QString &language : {QString("-"), QString("ru")}) {
            const auto row = [&](const char *name, const char *format, const char *method,
                                 UInt32 dictionary, UInt64 expectedDictionary, UInt32 order,
                                 UInt32 expectedOrder, UInt32 block, UInt32 expectedBlock) {
                QTest::newRow(qPrintable(QString(name) + '-' + language))
                    << QString(format) << QString(method) << dictionary << expectedDictionary
                    << order << expectedOrder << block << expectedBlock << language;
            };
            row("lzma-between", "7z", "LZMA", 5 << 20, 4 << 20, 50, 48, 63, 36);
            row("lzma-below", "7z", "LZMA", 1, 64 << 10, 1, 8, 1, UInt32(-1));
            row("lzma-marker", "7z", "LZMA2", UInt32(-2),
                sizeof(size_t) == 8 ? UInt64(3840) << 20 : UInt64(64) << 20, 1000, 273, 64, 64);
            row("ppmd-between", "7z", "PPMd", 5 << 20, 4 << 20, 15, 14, 0, 0);
            row("ppmd-marker", "7z", "PPMd", UInt32(-2), UInt64(1) << (29 + sizeof(size_t) / 8), 1,
                2, 19, UInt32(-1));
            row("bzip-decimal", "7z", "BZip2", 200000, 200 << 10, UInt32(-1), UInt32(-1), 0, 0);
            row("zip-deflate-between", "zip", "Deflate", 200000, UInt64(-1), 50, 48, UInt32(-1),
                UInt32(-1));
            row("auto", "7z", "LZMA2", UInt32(-1), UInt64(-1), UInt32(-1), UInt32(-1), UInt32(-1),
                UInt32(-1));
        }
    }
    void restoredValues() {
        QFETCH(QString, format);
        QFETCH(QString, method);
        QFETCH(UInt32, dictionary);
        QFETCH(UInt64, expectedDictionary);
        QFETCH(UInt32, order);
        QFETCH(UInt32, expectedOrder);
        QFETCH(UInt32, block);
        QFETCH(UInt32, expectedBlock);
        QFETCH(QString, language);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto previous = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previous);
        });
        const QString settings = fixture.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        NCompression::CInfo registry;
        registry.ArcType = format;
        auto &profile = registry.Formats.AddNew();
        profile.FormatID = format;
        profile.Method = method;
        profile.Level = 5;
        profile.Dictionary = dictionary;
        profile.Order = order;
        profile.BlockLogSize = block;
        profile.NumThreads = 999999;
        registry.Save(settings);
        QFile file(settings);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray before = file.readAll();
        file.close();
        for (int restart = 0; restart < 2; ++restart) {
            CreateArchiveDialog dialog(fixture.path());
            const auto options = dialog.compressionOptions();
            QCOMPARE(options.Dict64, expectedDictionary);
            QCOMPARE(options.Order, expectedOrder);
            QCOMPARE(options.NumThreads, UInt32(-1));
            QCOMPARE(dialog.findChild<QComboBox *>("compressionThreads")->currentIndex(), -1);
            auto *solid = dialog.findChild<QComboBox *>("compressionSolidBlock");
            QCOMPARE(solid->count() > 1 ? solid->currentData().toUInt() : UInt32(-1),
                     expectedBlock);
            if (format == "zip") {
                QCOMPARE(solid->count(), 0);
                QCOMPARE(solid->currentIndex(), -1);
            }
            if (restart == 0) {
                dialog.show();
                QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
                    auto *scroll = editor.findChild<QScrollArea *>("dialogContentScroll");
                    QVERIFY(scroll);
                    for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
                        editor.resize(size);
                        QApplication::processEvents();
                        for (const char *name : {"compressionDictionary", "compressionWordSize",
                                                 "compressionSolidBlock"}) {
                            auto *combo = editor.findChild<QComboBox *>(name);
                            QVERIFY(combo);
                            if (combo->count() == 0) {
                                QVERIFY(!combo->isVisible());
                                continue;
                            }
                            scroll->ensureWidgetVisible(combo, 0, 80);
                            QTRY_VERIFY_WITH_TIMEOUT(UiInspection::fullyVisible(*combo, editor),
                                                     1000);
                        }
                        QVERIFY(UiInspection::writeReport(editor, QString("profile-selection-%1-%2")
                                                                      .arg(QTest::currentDataTag())
                                                                      .arg(size.width())));
                    }
                }));
            }
            dialog.reject();
            QVERIFY(dialog.target().isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), before);
            file.close();
        }
    }
    void acceptedProfileCreatesAndExtracts() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto previous = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previous.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previous);
        });
        const QString settings = fixture.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        NCompression::CInfo registry;
        auto &profile = registry.Formats.AddNew();
        profile.FormatID = "7z";
        profile.Method = "LZMA";
        profile.Dictionary = 5 << 20;
        profile.Order = 50;
        profile.BlockLogSize = 63;
        registry.Save(settings);
        const QString input = fixture.filePath(QStringLiteral("данные 空.bin"));
        const QByteArray bytes("source\0bytes", 12);
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        CreateArchiveDialog dialog(fixture.path(), {input});
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto options = dialog.compressionOptions();
        QCOMPARE(options.Dict64, UInt64(4 << 20));
        QCOMPARE(options.Order, 48u);
        NCompression::CInfo saved;
        saved.Load(settings);
        QCOMPARE(saved.Formats[0].Dictionary, 4u << 20);
        QCOMPARE(saved.Formats[0].Order, 48u);
        QCOMPARE(saved.Formats[0].BlockLogSize, 36u);
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const auto completed = [&] {
            if (done.isEmpty() && !done.wait(15000))
                return false;
            const bool success = done[0][0].toBool();
            done.clear();
            return success;
        };
        QCOMPARE(
            SourceCreationFixture::Create(backend, dialog.target(), {input}, {}, false, options),
            S_OK);
        backend.list(dialog.target());
        QVERIFY(completed());
        backend.test(dialog.target());
        QVERIFY(completed());
        backend.extract(dialog.target(), fixture.filePath("output"));
        QVERIFY(completed());
        QFile output(fixture.filePath("output/" + QFileInfo(input).fileName()));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), bytes);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
};
QTEST_MAIN(CompressionProfileSelectionTests)
#include "compression_profile_selection_tests.moc"
