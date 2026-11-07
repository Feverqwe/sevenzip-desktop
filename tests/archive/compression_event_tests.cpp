#include "7zip/UI/GUI/CompressDialogRes.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/archive_expert.h"
#include "ui/support/layout_inspection.h"
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

class CompressionEventTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    QString settings_, input_, module_;
    QByteArray previous_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    const QByteArray bytes_ = QByteArray("source\0данные", 19);

    static QComboBox *combo(CreateArchiveDialog &dialog, const char *name) {
        return dialog.findChild<QComboBox *>(name);
    }
    void profile(UInt32 threads, UInt32 block = UInt32(-1)) {
        NCompression::CInfo registry;
        registry.ArcType = "xz";
        auto &format = registry.Formats.AddNew();
        format.FormatID = "xz";
        format.Method = "LZMA2";
        format.Level = 5;
        format.Dictionary = 1 << 20;
        format.NumThreads = threads;
        format.BlockLogSize = block;
        format.MemUse = "50M";
        registry.Save(settings_);
    }
    static QByteArray contents(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    void language(const QString &name) {
        SaveRegLang(UString(name.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
    }
    static void languageData() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }

    static QString submitAndDismissRefusal(CreateArchiveDialog &dialog) {
        QString messageText;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                messageText = message->text();
                message->accept();
            } else if (auto *editor = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                       editor && editor->objectName() == "archiveExpertDialog") {
                editor->reject();
            }
        });
        responder.start(1);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        return messageText;
    }

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module required");
#else
        QVERIFY(fixture_.isValid());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        catalog_ = std::make_shared<ArchiveCodecCatalog>(module_, 0);
        QCOMPARE(catalog_->result(), S_OK);
        settings_ = fixture_.filePath("settings.ini");
        input_ = fixture_.filePath(QStringLiteral("вход 空.bin"));
        QFile file(input_);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes_), qint64(bytes_.size()));
        previous_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
#endif
    }
    void init() { QFile::remove(settings_); }
    void cleanupTestCase() {
        if (previous_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previous_);
    }
    void levelKeepsEditedParameters_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("format");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QString &format : {QString("7z"), QString("zip"), QString("xz")})
                QTest::newRow(qPrintable(language + '-' + format)) << language << format;
    }
    void levelKeepsEditedParameters() {
        QFETCH(QString, language);
        QFETCH(QString, format);
        this->language(language);
        const QByteArray before = contents(settings_);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        combo(dialog, "archiveFormat")->setCurrentText(format);
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        parameters->setText("  mt=1  ");
        auto *level = combo(dialog, "compressionLevel");
        for (UInt32 value : {9u, 1u, 5u}) {
            const int index = level->findData(value);
            QVERIFY(index >= 0);
            level->setCurrentIndex(index);
            QCOMPARE(parameters->text(), QString("  mt=1  "));
            QCOMPARE(dialog.sourceInfo().Level, value);
            QCOMPARE(dialog.sourceInfo().Options, UString(L"mt=1"));
            QCOMPARE(dialog.sourceInfo().NumThreads, UInt32(-1));
        }
        dialog.reject();
        QCOMPARE(contents(settings_), before);
        QCOMPARE(contents(input_), bytes_);
    }
    void solidKeepsThreadSelection_data() {
        QTest::addColumn<UInt32>("threads");
        QTest::addColumn<QString>("language");
        for (const QString &lang : {QString("-"), QString("ru")})
            for (UInt32 threads : {UInt32(-1), UInt32(1), UInt32(999999)})
                QTest::newRow(qPrintable(lang + '-' + QString::number(threads))) << threads << lang;
    }
    void solidKeepsThreadSelection() {
        QFETCH(UInt32, threads);
        QFETCH(QString, language);
        profile(threads);
        this->language(language);
        const auto before = contents(settings_);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        QCOMPARE(combo(dialog, "archiveFormat")->currentText(), QString("xz"));
        auto *thread = combo(dialog, "compressionThreads");
        auto *solid = combo(dialog, "compressionSolidBlock");
        const int selection = thread->currentIndex();
        QCOMPARE(selection < 0, threads == 999999);
        for (UInt32 block : {20u, 64u, UInt32(-1)}) {
            const int index = solid->findData(block);
            QVERIFY(index >= 0);
            solid->setCurrentIndex(index);
            QCOMPARE(thread->currentIndex(), selection);
            QCOMPARE(dialog.compressionOptions().NumThreads,
                     threads == 999999 ? UInt32(-1) : threads);
        }
        dialog.reject();
        QCOMPARE(contents(settings_), before);
        QCOMPARE(contents(input_), bytes_);
    }
    void solidMemoryRecalculatesAuto_data() { languageData(); }
    void solidMemoryRecalculatesAuto() {
        QFETCH(QString, language);
        const auto system = NCompressDialog::GetSystemInfo();
        if (system.NumCPUs < 4 || !system.RamSizeDefined)
            QSKIP("Automatic thread adjustment requires four available CPUs and known RAM");
        profile(UInt32(-1));
        this->language(language);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        QCOMPARE(combo(dialog, "archiveFormat")->currentText(), QString("xz"));
        auto *thread = combo(dialog, "compressionThreads");
        QCOMPARE(thread->itemText(0), NCompressDialog::Modify_Auto(QString("2")));
        auto *solid = combo(dialog, "compressionSolidBlock");
        solid->setCurrentIndex(solid->findData(20u));
        const auto options = dialog.compressionOptions();
        const auto adjusted =
            NCompressDialog::SetNumThreads2(QStringLiteral("xz"), NCompressDialog::kLZMA2, 5,
                                            1 << 20, 20, system, options.MemUsage);
        QVERIFY(adjusted.AutoNumThreads > 2);
        UInt64 decompress;
        const UInt64 usage = NCompressDialog::GetMemoryUsage_Threads_Dict_DecompMem(
            QStringLiteral("xz"), NCompressDialog::kLZMA2, 5, adjusted.AutoNumThreads, 1 << 20, 20,
            decompress);
        QCOMPARE(dialog.findChild<QLabel *>("compressionMemoryUsage")->text(),
                 CCompressDialog::PrintMemUsage(IDT_COMPRESS_MEMORY_VALUE, usage, options.MemUsage,
                                                system));
        QCOMPARE(thread->itemText(0),
                 NCompressDialog::Modify_Auto(QString::number(adjusted.AutoNumThreads)));
        dialog.show();
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            auto *scroll = editor.findChild<QScrollArea *>("dialogContentScroll");
            auto *memory = editor.findChild<QLabel *>("compressionMemoryUsage");
            QVERIFY(scroll && memory);
            for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
                editor.resize(size);
                QApplication::processEvents();
                scroll->ensureWidgetVisible(thread);
                QTRY_VERIFY(UiInspection::fullyVisible(*thread, editor));
                scroll->ensureWidgetVisible(memory);
                QTRY_VERIFY(UiInspection::fullyVisible(*memory, editor));
                QVERIFY(UiInspection::labelFits(*memory));
                QVERIFY(UiInspection::writeReport(
                    editor, QString("compression-event-%1-%2").arg(language).arg(size.width())));
            }
        }));
        auto *limit = combo(dialog, "compressionMemoryLimit");
        limit->setCurrentIndex(limit->findData("256M"));
        const auto recalculated = NCompressDialog::SetNumThreads2(
            QStringLiteral("xz"), NCompressDialog::kLZMA2, 5, 1 << 20, 20, system,
            dialog.compressionOptions().MemUsage);
        QCOMPARE(thread->itemText(0),
                 NCompressDialog::Modify_Auto(QString::number(recalculated.AutoNumThreads)));
        QCOMPARE(thread->currentIndex(), 0);
        auto *dictionary = combo(dialog, "compressionDictionary");
        dictionary->setCurrentIndex(dictionary->findData(qulonglong(2 << 20)));
        const auto afterDictionary = NCompressDialog::SetNumThreads2(
            QStringLiteral("xz"), NCompressDialog::kLZMA2, 5, 2 << 20, UInt32(-1), system,
            dialog.compressionOptions().MemUsage);
        QCOMPARE(thread->itemText(0),
                 NCompressDialog::Modify_Auto(QString::number(afterDictionary.AutoNumThreads)));
        QVERIFY(!dialog.compressionOptions().SolidIsSpecified);
        dialog.reject();
    }
    void sourceMemoryRefusalDoesNotSave_data() { languageData(); }
    void sourceMemoryRefusalDoesNotSave() {
        QFETCH(QString, language);
        const auto system = NCompressDialog::GetSystemInfo();
        if (system.NumCPUs < 4 || !system.RamSizeDefined)
            QSKIP("Automatic thread adjustment requires four available CPUs and known RAM");
        profile(UInt32(-1), 64);
        this->language(language);
        const auto before = contents(settings_);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        QCOMPARE(combo(dialog, "archiveFormat")->currentText(), QString("xz"));
        auto *threads = combo(dialog, "compressionThreads");
        const int fourThreads = threads->findData(4u);
        QVERIFY(fourThreads >= 0);
        threads->setCurrentIndex(fourThreads);
        auto *solid = combo(dialog, "compressionSolidBlock");
        solid->setCurrentIndex(solid->findData(UInt32(-1)));
        QCOMPARE(dialog.compressionOptions().NumThreads, 4u);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QVERIFY(submitAndDismissRefusal(dialog).contains(
            QString::fromUtf8(language == "ru" ? "50 МБ" : "50 MB")));
        QCOMPARE(accepted.size(), 0);
        QCOMPARE(dialog.result(), 0);
        QCOMPARE(contents(settings_), before);
        dialog.reject();
        QCOMPARE(contents(input_), bytes_);
    }
    void savedEventProfileRunsOfficialEngine_data() { languageData(); }
    void savedEventProfileRunsOfficialEngine() {
        QFETCH(QString, language);
        profile(UInt32(-1), 64);
        this->language(language);
        {
            CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
            QCOMPARE(combo(dialog, "archiveFormat")->currentText(), QString("xz"));
            auto *solid = combo(dialog, "compressionSolidBlock");
            solid->setCurrentIndex(solid->findData(UInt32(-1)));
            const QString refusal = submitAndDismissRefusal(dialog);
            QVERIFY2(refusal.isEmpty(), qPrintable(refusal));
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        CreateArchiveDialog restarted(fixture_.path(), {input_}, nullptr, catalog_);
        const auto options = restarted.compressionOptions();
        QCOMPARE(options.format, QString("xz"));
        QCOMPARE(options.NumThreads, UInt32(-1));
        QVERIFY(!options.SolidIsSpecified);
        QCOMPARE(options.Dict64, UInt64(1 << 20));
        QCOMPARE(options.MemUsage.GetBytes(0), UInt64(50 << 20));
        restarted.reject();
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const QString target = fixture_.filePath("event " + language + " 空.xz");
        QCOMPARE(SourceCreationFixture::Create(backend, target, {input_}, {}, false, options),
                 S_OK);
        QList<ArchiveEntry> entries;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &value) { entries = value; });
        done.clear();
        backend.list(target);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().size, qint64(bytes_.size()));
        done.clear();
        backend.test(target);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        done.clear();
        const QString output = fixture_.filePath("output " + language);
        backend.extract(target, output);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        QCOMPARE(contents(QDir(output).filePath(entries.first().path)), bytes_);
        const QString failed = fixture_.filePath("invalid " + language + ".xz");
        auto invalid = options;
        invalid.NumThreads = 999999;
        done.clear();
        QVERIFY(SourceCreationFixture::Create(backend, failed, {input_}, {}, false, invalid) !=
                S_OK);
        QVERIFY(!QFileInfo::exists(failed));
        const QString cancelled = fixture_.filePath("cancel " + language + ".xz");
        done.clear();
        QCOMPARE(
            SourceCreationFixture::Create(backend, cancelled, {input_}, {}, false, options, true),
            E_ABORT);
        QVERIFY(!QFileInfo::exists(cancelled));
        QCOMPARE(contents(input_), bytes_);
    }
};
QTEST_MAIN(CompressionEventTests)
#include "compression_event_tests.moc"
