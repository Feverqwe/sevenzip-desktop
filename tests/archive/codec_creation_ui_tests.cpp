#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class CodecCreationUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    QString source_, engine_, module_;
    QByteArray previousSettings_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;

  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native creation UI remains unverified");
#else
        QVERIFY(fixture_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", fixture_.filePath("settings.ini").toUtf8());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        engine_ = QStandardPaths::findExecutable("7z");
        if (engine_.isEmpty())
            QSKIP("Official engine unavailable; native creation UI remains unverified");
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        catalog_ = backend.codecCatalog();
        source_ = fixture_.filePath(QStringLiteral("источник 空 with spaces.txt"));
        QFile source(source_);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("payload"), qint64(7));
#endif
    }
    void init() { QFile::remove(fixture_.filePath("settings.ini")); }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void malformedParametersCannotCreateAnArchive() {
        CompressionOptions options;
        options.format = "zip";
        const auto text = (QString("tm=off") + QChar::Null + " mt=1").toStdWString();
        options.Options.SetFrom(text.data(), unsigned(text.size()));
        CObjectVector<CProperty> properties;
        QString error;
        QVERIFY(
            !ArchiveBackend::compressionProperties("zip", {}, false, options, &properties, &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir output;
        QVERIFY(output.isValid());
        const QString target = output.filePath(QStringLiteral("архив 空.zip"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source_}, {}, false, options),
                 E_INVALIDARG);
        QVERIFY(QDir(output.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
        options.Options = "tm=off mt=1";
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source_}, {}, false, options),
                 S_OK);
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(
            ArchiveBackend::openNativeArchive(catalog_, target, "zip", nullptr, reader, nullptr),
            S_OK);
        QVERIFY(reader);
    }

    void selectedFormatCreatesRealArchive_data() {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QSize>("size");
        for (const QString &type : {"7z", "zip", "gzip", "bzip2", "xz", "tar", "wim"})
            for (const QSize size : {QSize(760, 720), QSize(640, 480)})
                QTest::newRow(qPrintable(type + QString::number(size.width()))) << type << size;
    }
    void selectedFormatCreatesRealArchive() {
        QFETCH(QString, type);
        QFETCH(QSize, size);
        CreateArchiveDialog dialog(fixture_.path(), {source_}, nullptr, catalog_);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        const int index = format->findText(type);
        QVERIFY(index >= 0);
        format->setCurrentIndex(index);
        if (type == "tar") {
            auto *method = dialog.findChild<QComboBox *>("compressionMethod");
            QVERIFY(method->isEnabled());
            method->setCurrentIndex(method->findText("POSIX"));
            QCOMPARE(method->currentText(), QStringLiteral("POSIX"));
        }
        dialog.resize(size);
        dialog.show();
        QApplication::processEvents();
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        password->setText("retained password");
        const bool encrypt = type == "7z" || type == "zip";
        QCOMPARE(password->isEnabled(), encrypt);
        if (!encrypt)
            QVERIFY(dialog.password().isEmpty());
        password->clear();
        auto *scroll = dialog.findChild<QScrollArea *>();
        scroll->ensureWidgetVisible(format);
        QApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*format, dialog));
        QVERIFY(UiInspection::writeReport(dialog,
                                          type + "-creation-top-" + QString::number(size.width())));
        auto *group = dialog.findChild<QGroupBox *>("archiveEncryptionGroup");
        QCOMPARE(group->isEnabled(), encrypt);
        QCOMPARE(group->isVisible(), encrypt);
        if (encrypt) {
            scroll->ensureWidgetVisible(group);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*group, dialog));
        }
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(
            UiInspection::writeReport(dialog, type + "-creation-" + QString::number(size.width())));
        const auto options = dialog.compressionOptions();
        QCOMPARE(options.format, type);
        if (type == "tar")
            QCOMPARE(QString::fromWCharArray(options.Method.Ptr()), QStringLiteral("POSIX"));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(),
                                               dialog.password(), dialog.encryptHeaders(), options),
                 S_OK);
        QVERIFY(QFileInfo::exists(dialog.target()));
        if (type == "tar") {
            // TarHeader.cpp defines POSIX versus GNU magic. This observes the
            // actual writer result, not only the dialog's requested method.
            QFile archive(dialog.target());
            QVERIFY(archive.open(QIODevice::ReadOnly));
            QCOMPARE(archive.read(512).mid(257, 8), QByteArray("ustar\0"
                                                               "00",
                                                               8));
        }
        QVERIFY(QFile::remove(dialog.target()));
    }
    void zipZstdSelection_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("language");
        for (const QSize size : {QSize(760, 720), QSize(640, 480)})
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(qPrintable(QString::number(size.width()) + language))
                    << size << language;
    }
    void zipZstdSelection() {
        QFETCH(QSize, size);
        QFETCH(QString, language);
        SaveRegLang(UString(language.toStdWString().c_str()), fixture_.filePath("settings.ini"));
        ReloadLang(fixture_.filePath("settings.ini"));
        const QString source = fixture_.filePath("zstd payload.txt");
        QFile payload(source);
        QVERIFY(payload.open(QIODevice::WriteOnly));
        const QByteArray bytes = QByteArray("Zstandard UI payload\n").repeated(10000);
        QCOMPARE(payload.write(bytes), qint64(bytes.size()));
        payload.close();
        CreateArchiveDialog dialog(fixture_.path(), {source}, nullptr, catalog_);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        format->setCurrentIndex(format->findText("zip"));
        QVERIFY(method->currentText().contains("Deflate"));
        const int zstd = method->findText("ZSTD");
        QVERIFY(zstd >= 0);
        method->setCurrentIndex(zstd);
        QCOMPARE(QString::fromWCharArray(dialog.compressionOptions().Method.Ptr()),
                 QString("ZSTD"));
        QVERIFY(!dialog.findChild<QComboBox *>("compressionDictionary")->isEnabled());
        QVERIFY(!dialog.findChild<QComboBox *>("compressionWordSize")->isEnabled());
        ExpandArchiveSections(dialog);
        dialog.resize(size);
        dialog.show();
        QApplication::processEvents();
        dialog.findChild<QScrollArea *>()->ensureWidgetVisible(method);
        QApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*method, dialog));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(UiInspection::writeReport(
            dialog, QString("zip-zstd-%1-%2").arg(size.width()).arg(language)));
        const CompressionOptions options = dialog.compressionOptions();
        CObjectVector<CProperty> properties;
        QString error;
        QVERIFY2(
            ArchiveBackend::compressionProperties("zip", {}, false, options, &properties, &error),
            qPrintable(error));
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                               false, options),
                 S_OK);
        QFile archive(dialog.target());
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QByteArray header = archive.read(10);
        QCOMPARE(header.size(), 10);
        QCOMPARE(quint8(header[8]) | (quint16(quint8(header[9])) << 8), 93);
        archive.close();
        QVERIFY(QFile::remove(dialog.target()));
        format->setCurrentIndex(format->findText("7z"));
        QCOMPARE(method->findText("ZSTD"), -1);
    }
    void sourceSelectionDeterminesFormatParameters() {
        const QString folder = fixture_.filePath(QStringLiteral("папка 空"));
        QVERIFY(QDir().mkpath(folder));
        CreateArchiveDialog dialog(fixture_.path(), {source_}, nullptr, catalog_);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentIndex(format->findText("gzip"));
        QVERIFY(!dialog.findChild<QLineEdit *>("archivePassword")->isEnabled());
        CreateArchiveDialog multiple(fixture_.path(), {source_, folder}, nullptr, catalog_);
        QCOMPARE(multiple.sources().size(), 2);
        auto *types = multiple.findChild<QComboBox *>("archiveFormat");
        QCOMPARE(types->findText("gzip"), -1);
        types->setCurrentText("7z");
        auto *passwordEnabled = multiple.findChild<QCheckBox *>("archivePasswordEnabled");
        QVERIFY(passwordEnabled && passwordEnabled->isEnabled());
        passwordEnabled->click();
        QVERIFY(multiple.findChild<QLineEdit *>("archivePassword")->isEnabled());
        QCOMPARE(multiple.compressionOptions().format, QStringLiteral("7z"));
        QVERIFY(
            multiple.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
    }
};
QTEST_MAIN(CodecCreationUiTests)
#include "codec_creation_ui_tests.moc"
