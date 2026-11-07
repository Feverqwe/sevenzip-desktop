#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include <QFile>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class SfxLifecycleTests final : public QObject {
    Q_OBJECT
    static void saveProfile(const QString &settings, const QString &language, const QString &method,
                            UInt32 threads) {
        NCompression::CInfo registry;
        registry.ArcType = "7z";
        auto &profile = registry.Formats.AddNew();
        profile.FormatID = "7z";
        profile.Method = method;
        profile.Level = 5;
        profile.Dictionary = 1 << 20;
        profile.Order = 64;
        profile.NumThreads = threads;
        profile.BlockLogSize = 24;
        profile.MemUse = "256M";
        registry.Save(settings);
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
    }

  private slots:
    void toggleKeepsMethodControls_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("method");
        QTest::addColumn<UInt32>("threads");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QString &method : {QString(), QString("LZMA"), QString("LZMA2")})
                for (UInt32 threads : {1u, 999999u})
                    QTest::newRow(
                        qPrintable(language + '-' + method + '-' + QString::number(threads)))
                        << language << method << threads;
    }
    void toggleKeepsMethodControls() {
#if !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Official native codec module required");
#else
        QFETCH(QString, language);
        QFETCH(QString, method);
        QFETCH(UInt32, threads);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString sfx = QString::fromUtf8(SEVENZIP_SFX_FIXTURE);
        QVERIFY(QFileInfo::exists(sfx));
        const QByteArray previousSfx = qgetenv("SEVENZIP_SFX_MODULE");
        const QByteArray previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previousSfx.isNull())
                qunsetenv("SEVENZIP_SFX_MODULE");
            else
                qputenv("SEVENZIP_SFX_MODULE", previousSfx);
            if (previousSettings.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previousSettings);
        });
        const QString settings = fixture.filePath("settings.ini");
        qputenv("SEVENZIP_SFX_MODULE", sfx.toUtf8());
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        saveProfile(settings, language, method, threads);
        const QByteArray before = read(settings);
        const QString input = fixture.filePath(QStringLiteral("input 空 with spaces.txt"));
        QVERIFY(write(input, "SFX controls"));
        auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        CreateArchiveDialog dialog(fixture.path(), {input}, nullptr, catalog);
        auto *sfxControl = dialog.findChild<QCheckBox *>("archiveSfx");
        QVERIFY(sfxControl && sfxControl->isEnabled());
        auto *thread = dialog.findChild<QComboBox *>("compressionThreads");
        const int selectedThread = thread->currentIndex();
        QCOMPARE(selectedThread < 0, threads == 999999);
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        parameters->setText("  mt=1  ");
        const auto original = dialog.sourceInfo();
        const auto options = dialog.compressionOptions();
        for (bool checked : {true, false, true, false}) {
            sfxControl->setChecked(checked);
            QCOMPARE(thread->currentIndex(), selectedThread);
            QCOMPARE(parameters->text(), QString("  mt=1  "));
            const auto current = dialog.sourceInfo();
            QCOMPARE(current.Method, original.Method);
            QCOMPARE(current.Level, original.Level);
            QCOMPARE(current.Dict64, original.Dict64);
            QCOMPARE(current.Order, original.Order);
            QCOMPARE(current.NumThreads, original.NumThreads);
            QCOMPARE(current.SolidBlockSize, original.SolidBlockSize);
            QCOMPARE(current.MemUsage.GetBytes(1 << 30), original.MemUsage.GetBytes(1 << 30));
            QCOMPARE(dialog.compressionOptions().SFXMode, checked);
        }
        QCOMPARE(dialog.compressionOptions().format, options.format);
        dialog.reject();
        QCOMPARE(read(settings), before);
        QCOMPARE(read(input), QByteArray("SFX controls"));
#endif
    }
    void acceptedFormCreatesAndDeletes_data() {
        QTest::addColumn<bool>("volumes");
        QTest::addColumn<QString>("outcome");
        for (bool volumes : {false, true})
            for (const QString &outcome : {"success", "cancel"})
                QTest::newRow(qPrintable(QString::number(volumes) + outcome)) << volumes << outcome;
    }
    void acceptedFormCreatesAndDeletes() {
#if !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Official native codec module unavailable; lifecycle remains unverified");
#else
        QFETCH(bool, volumes);
        QFETCH(QString, outcome);
        const QString sfx = QString::fromUtf8(SEVENZIP_SFX_FIXTURE);
        if (!QFileInfo::exists(sfx))
            QSKIP("Run scripts/build-sfx.sh for the official native module");
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray previousSfx = qgetenv("SEVENZIP_SFX_MODULE");
        const QByteArray previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const auto restore = qScopeGuard([&] {
            if (previousSfx.isNull())
                qunsetenv("SEVENZIP_SFX_MODULE");
            else
                qputenv("SEVENZIP_SFX_MODULE", previousSfx);
            if (previousSettings.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previousSettings);
        });
        qputenv("SEVENZIP_SFX_MODULE", sfx.toUtf8());
        qputenv("SEVENZIP_SETTINGS_PATH", fixture.filePath("settings.ini").toUtf8());
        QByteArray bytes(2 * 1024 * 1024, Qt::Uninitialized);
        QRandomGenerator random(0x7a5f);
        for (int i = 0; i < bytes.size(); ++i)
            bytes[i] = char(random.generate() & 0xff);
        QVERIFY(QDir(fixture.path()).mkpath("first"));
        QVERIFY(QDir(fixture.path()).mkpath("second"));
        const QString first = fixture.filePath(QStringLiteral("first/данные 空 with spaces.bin"));
        const QString second = fixture.filePath("second/other.txt");
        QVERIFY(write(first, bytes));
        QVERIFY(write(second, "second source"));
        ArchiveBackend backend(nullptr, QStandardPaths::findExecutable("7z"));
        ArchiveCodecCatalog probe(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(probe.result(), S_OK);
        QCOMPARE(
            backend.loadCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE), probe.version()),
            S_OK);
        CreateArchiveDialog dialog(fixture.path(), {first, second}, nullptr,
                                   backend.codecCatalog());
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("7z");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        level->setCurrentIndex(0);
        auto *pathMode = dialog.findChild<QComboBox *>("archivePathMode");
        pathMode->setCurrentIndex(pathMode->findData(int(NWildcard::k_RelatPath)));
        auto *sfxControl = dialog.findChild<QCheckBox *>("archiveSfx");
        QVERIFY(sfxControl && sfxControl->isEnabled());
        sfxControl->setChecked(true);
#ifdef Q_OS_WIN
        dialog.findChild<QLineEdit *>("archiveName")->setText("accepted self extractor.exe");
#else
        dialog.findChild<QLineEdit *>("archiveName")->setText("accepted self extractor");
#endif
        if (volumes)
            dialog.findChild<QComboBox *>("archiveVolumeSize")->setEditText("1M");
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(dialog.compressionOptions().SFXMode);
        const QString target = dialog.target();
        const QByteArray originalModule = read(sfx);
        const HRESULT result = SourceCreationFixture::Create(
            backend, target, dialog.sources(), dialog.password(), dialog.encryptHeaders(),
            dialog.compressionOptions(), outcome == "cancel");
        QCOMPARE(read(sfx), originalModule);
        if (outcome == "cancel") {
            QCOMPARE(result, E_ABORT);
            QCOMPARE(read(first), bytes);
            QCOMPARE(read(second), QByteArray("second source"));
            QVERIFY(!QFileInfo::exists(target));
            QVERIFY(QDir(fixture.path()).entryList({"*.7z.*"}, QDir::Files).isEmpty());
        } else {
            QCOMPARE(result, S_OK);
            QCOMPARE(read(first), bytes);
            QCOMPARE(read(second), QByteArray("second source"));
            QVERIFY(QFileInfo(target).isExecutable());
            QProcess extractor;
            const QString output = fixture.filePath("extracted");
            QStringList arguments{"-y", "-o" + output};
#ifndef Q_OS_WIN
            arguments.prepend("x");
#endif
            extractor.start(target, arguments);
            QVERIFY(extractor.waitForFinished(10000));
            QCOMPARE(extractor.exitCode(), 0);
            QCOMPARE(read(QDir(output).filePath(QFileInfo(first).fileName())), bytes);
            QCOMPARE(read(QDir(output).filePath(QFileInfo(second).fileName())),
                     QByteArray("second source"));
        }
        QVERIFY(
            QDir(fixture.path()).entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden).isEmpty());
#endif
    }
};
QTEST_MAIN(SfxLifecycleTests)
#include "sfx_lifecycle_tests.moc"
