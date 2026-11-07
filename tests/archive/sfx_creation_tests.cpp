#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/sfx/archivesfxmodule.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/archive_expert.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QToolButton>
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
class SfxCreationTests final : public QObject {
    Q_OBJECT
    QString engine_, module_, sfx_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#if !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Official native SFX fixture unavailable; platform SFX consumer unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        sfx_ = QString::fromUtf8(SEVENZIP_SFX_FIXTURE);
        if (!QFileInfo::exists(sfx_))
            QSKIP("Run scripts/build-sfx.sh to provide the official native SFX fixture");
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        version_ = backend.codecCatalog()->version();
        const auto sfx = backend.sfxModule(sfx_);
        QVERIFY2(sfx.available(), qPrintable(sfx.error()));
#endif
    }
    void provenanceRejectsChanges() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("copied module.sfx");
        const auto bytes = read(sfx_);
        QVERIFY(write(path, bytes));
        QVERIFY(write(path + ".json", read(sfx_ + ".json")));
        QVERIFY(ArchiveSfxModule::Read(path, version_).available());
        QVERIFY(!ArchiveSfxModule::Read(path, version_ + 1).available());
        QJsonObject metadata = QJsonDocument::fromJson(read(path + ".json")).object();
        metadata["system"] = "other platform";
        QVERIFY(write(path + ".json", QJsonDocument(metadata).toJson()));
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
        QVERIFY(write(path + ".json", read(sfx_ + ".json")));
        QByteArray changed = bytes;
        changed[changed.size() / 2] = char(changed[changed.size() / 2] ^ 1);
        QVERIFY(write(path, changed));
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
        QFile::remove(path + ".json");
        QVERIFY(!ArchiveSfxModule::Read(path, version_).available());
    }
    void matchingMetadataDoesNotAcceptInvalidExecutable() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("invalid native module.sfx");
        const QByteArray bytes("This is not an executable module.");
        QVERIFY(write(path, bytes));
        QJsonObject metadata = QJsonDocument::fromJson(read(sfx_ + ".json")).object();
        metadata["sha256"] = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        QVERIFY(write(path + ".json", QJsonDocument(metadata).toJson()));
        QVERIFY(ArchiveSfxModule::Read(path, version_).available());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QVERIFY(!backend.sfxModule(path).available());
        const QString source = fixture.filePath(QStringLiteral("данные with spaces.txt"));
        QVERIFY(write(source, "source bytes"));
        const QString target = fixture.filePath("output");
        CompressionOptions options;
        options.format = "7z";
        options.SFXMode = true;
        options.SfxModule = path;
        QVERIFY(SourceCreationFixture::Create(backend, target, {source}, {}, false, options) !=
                S_OK);
        QVERIFY(!QFileInfo::exists(target));
        QCOMPARE(read(source), QByteArray("source bytes"));
        QCOMPARE(read(path), bytes);
    }
    void backendCreatesSelfTestingExecutable_data() {
        QTest::addColumn<bool>("multiRoot");
        QTest::addColumn<bool>("volumes");
        QTest::addColumn<QString>("outcome");
        for (bool multiRoot : {false, true})
            for (bool volumes : {false, true})
                for (const QString &outcome : {"success", "missing", "cancel", "occupied"})
                    QTest::newRow(
                        qPrintable(QString::number(multiRoot) + QString::number(volumes) + outcome))
                        << multiRoot << volumes << outcome;
    }
    void backendCreatesSelfTestingExecutable() {
        QFETCH(bool, multiRoot);
        QFETCH(bool, volumes);
        QFETCH(QString, outcome);
        QTemporaryDir fixture, second;
        QVERIFY(fixture.isValid() && second.isValid());
        const QString name = QStringLiteral("имя 空 with spaces.bin");
        const QByteArray bytes("binary\0fixture\xff", 15);
        QVERIFY(write(fixture.filePath(name), bytes));
        QVERIFY(write(second.filePath("other.txt"), "other root"));
        QStringList sources{fixture.filePath(name)};
        if (multiRoot)
            sources.append(second.filePath("other.txt"));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        CompressionOptions options;
        options.format = "7z";
        options.SFXMode = true;
        options.SfxModule = outcome == "missing" ? fixture.filePath("missing.sfx") : sfx_;
        if (volumes)
            options.VolumeSizes.Add(32);
        const QString base = fixture.filePath("self extracting archive");
#ifdef Q_OS_WIN
        const QString target = base + ".exe";
#else
        const QString target = base;
#endif
        const QString occupied = volumes ? base + ".7z.001" : target;
        if (outcome == "occupied")
            QVERIFY(write(occupied, "foreign output"));
        const QByteArray originalModule = read(sfx_);
        const QString password = multiRoot ? QStringLiteral("local fixture password") : QString();
        const HRESULT result = SourceCreationFixture::Create(
            backend, target, sources, password, multiRoot, options, outcome == "cancel");
        QCOMPARE(result == S_OK, outcome == "success");
        QCOMPARE(read(fixture.filePath(name)), bytes);
        QCOMPARE(read(sfx_), originalModule);
        if (outcome != "success") {
            if (outcome == "occupied")
                QCOMPARE(read(occupied), QByteArray("foreign output"));
            if (outcome != "occupied" || volumes)
                QVERIFY(!QFileInfo::exists(target));
            if (volumes && outcome != "occupied")
                QVERIFY(QDir(fixture.path()).entryList({"*.7z.*"}, QDir::Files).isEmpty());
            QVERIFY(QDir(fixture.path())
                        .entryList({".sevenzip-*"}, QDir::Files | QDir::Hidden)
                        .isEmpty());
            return;
        }
        QVERIFY(QFileInfo(target).isExecutable());
        if (volumes) {
            QCOMPARE(read(target), originalModule);
            QVERIFY(QFileInfo::exists(base + ".7z.002"));
            QFile first(base + ".7z.001");
            QVERIFY(first.open(QIODevice::ReadOnly));
            QCOMPARE(first.read(6), QByteArray::fromHex("377abcaf271c"));
        }
        QProcess process;
        QStringList testArgs{"t"};
        if (!password.isEmpty())
            testArgs.append("-p" + password);
#ifdef Q_OS_WIN
        // SFXWin has no test command: its source always extracts. Test the
        // embedded archive/payload with the official engine, then run the GUI.
        testArgs << "--" << (volumes ? base + ".7z.001" : target);
        process.start(engine_, testArgs);
#else
        process.start(target, testArgs);
#endif
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const QString output = fixture.filePath("extracted");
        QStringList extractArgs{"x", "-o" + output, "-y"};
#ifdef Q_OS_WIN
        extractArgs.removeFirst(); // SfxWin.cpp accepts only -o/-y/-p.
#endif
        if (!password.isEmpty())
            extractArgs.append("-p" + password);
        process.start(target, extractArgs);
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(read(output + "/" + name), bytes);
        QCOMPARE(QFileInfo::exists(output + "/other.txt"), multiRoot);
    }
    void formTransitionsAndGeometry_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("language");
        for (const QString &language : {"-", "ru"})
            for (const QSize size : {QSize(760, 720), QSize(640, 480)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << size << language;
    }
    void formTransitionsAndGeometry() {
        QFETCH(QSize, size);
        QFETCH(QString, language);
        QTemporaryDir fixture;
        const UString previousLanguage = g_LangID;
        const QString languagePath = fixture.filePath("language.ini");
        const QByteArray previousSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const QByteArray previousSfx = qgetenv("SEVENZIP_SFX_MODULE");
        const auto restore = qScopeGuard([&] {
            if (previousSettings.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", previousSettings);
            if (previousSfx.isNull())
                qunsetenv("SEVENZIP_SFX_MODULE");
            else
                qputenv("SEVENZIP_SFX_MODULE", previousSfx);
            SaveRegLang(previousLanguage, languagePath);
            ReloadLang(languagePath);
            QtNotifyLanguageChanged();
        });
        SaveRegLang(UString(language.toStdWString().c_str()), languagePath);
        ReloadLang(languagePath);
        QtNotifyLanguageChanged();
        qputenv("SEVENZIP_SETTINGS_PATH", fixture.filePath("settings.ini").toUtf8());
        qputenv("SEVENZIP_SFX_MODULE", sfx_.toUtf8());
        const QString source = fixture.filePath(QStringLiteral("имя 空.txt"));
        QVERIFY(write(source, "form fixture"));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        CreateArchiveDialog dialog(fixture.path(), {source}, nullptr, backend.codecCatalog());
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        auto *sfx = dialog.findChild<QCheckBox *>("archiveSfx");
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *shared = dialog.findChild<QCheckBox *>("archiveSharedFiles");
        QVERIFY(format && method && sfx && name && shared);
        ExpandArchiveSections(dialog);
        format->setCurrentText("7z");
        QVERIFY(sfx->isEnabled());
        name->setText(QStringLiteral("пакет 空.7z"));
        sfx->setChecked(true);
        QVERIFY(dialog.compressionOptions().SFXMode);
        QCOMPARE(dialog.compressionOptions().SfxModule, sfx_);
#ifdef Q_OS_WIN
        QCOMPARE(name->text(), QStringLiteral("пакет 空.exe"));
#else
        QCOMPARE(name->text(), QStringLiteral("пакет 空"));
#endif
        QCOMPARE(method->findText("BZip2"), -1);
        dialog.resize(size);
        dialog.show();
        QApplication::processEvents();
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            editor.resize(size);
            auto *scroll = editor.findChild<QScrollArea *>();
            QVERIFY(scroll);
            scroll->ensureWidgetVisible(sfx->parentWidget());
            QApplication::processEvents();
            QVERIFY(UiInspection::writeReport(editor, "sfx-options-" + language + "-" +
                                                          QString::number(size.width())));
            for (const auto *control : {sfx, shared}) {
#ifndef Q_OS_WIN
                if (control == shared) {
                    QVERIFY(!control->isVisible());
                    continue;
                }
#endif
                QVERIFY2(UiInspection::fullyVisible(*control, editor),
                         qPrintable(control->objectName()));
                QVERIFY2(control->width() >= control->sizeHint().width(),
                         qPrintable(control->objectName()));
                QVERIFY(control->height() >= control->sizeHint().height());
            }
            if (shared->isVisible()) {
                QVERIFY(UiInspection::bounds(*sfx, editor).bottom() <
                        UiInspection::bounds(*shared, editor).top());
            }
        }));

        sfx->setChecked(false);
        QCOMPARE(name->text(), QStringLiteral("пакет 空.7z"));
        QVERIFY(method->findText("BZip2") >= 0);
        method->setCurrentText("BZip2");
        QVERIFY(!sfx->isEnabled());
        method->setCurrentIndex(method->findData(-1));
        QVERIFY(sfx->isEnabled());
        sfx->setChecked(true);
        format->setCurrentText("zip");
        QVERIFY(!sfx->isEnabled());
        QVERIFY(!sfx->isChecked());
        QVERIFY(!dialog.compressionOptions().SFXMode);
        QVERIFY(name->text().endsWith(".zip"));
        // Pinned native 7zFM preserves SetMethod2's SFX filter for this
        // transition, then unchecks SFX: ZIP's supported LZMA remains alone
        // but unselected (CB_GETCURSEL=-1 in the actual Windows reference).
        QCOMPARE(method->count(), 1);
        QCOMPARE(method->itemText(0), QString("LZMA"));
        QCOMPARE(method->currentIndex(), -1);
        QVERIFY(method->currentText().isEmpty());
        QVERIFY(!method->isEnabled());
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        qputenv("SEVENZIP_SFX_MODULE", fixture.filePath("missing.sfx").toUtf8());
        CreateArchiveDialog missing(fixture.path(), {source}, nullptr, backend.codecCatalog());
        QVERIFY(!missing.findChild<QCheckBox *>("archiveSfx")->isEnabled());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SfxCreationTests)
#include "sfx_creation_tests.moc"
