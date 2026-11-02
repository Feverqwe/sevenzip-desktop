#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/UpdateGUI.h"
#include "localization/LangUtils.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QCollator>
#include <QCryptographicHash>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cstdlib>

class CompressionFormatTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture_;
    QString previousSettings_;
    bool previousSettingsSet_ = false;
    QString settings_;
    QString input_;
    QString module_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
    static void setSettingsOverride(const QString &path) {
#ifdef Q_OS_WIN
        // qputenv uses the narrow CRT encoding on Windows, not UTF-8.
        _wputenv_s(L"SEVENZIP_SETTINGS_PATH", path.toStdWString().c_str());
#else
        qputenv("SEVENZIP_SETTINGS_PATH", path.toUtf8());
#endif
    }

  private slots:
    void initTestCase() {
        QVERIFY(fixture_.isValid());
        settings_ = fixture_.filePath("format 空.ini");
        input_ = fixture_.filePath("input 空 with spaces.txt");
        QFile file(input_);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("format selection bytes"), qint64(22));
        previousSettingsSet_ = qEnvironmentVariableIsSet("SEVENZIP_SETTINGS_PATH");
        previousSettings_ = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        setSettingsOverride(settings_);
        QCOMPARE(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), settings_);
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable");
#else
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        catalog_ = std::make_shared<ArchiveCodecCatalog>(module_, 0);
        QCOMPARE(catalog_->result(), S_OK);
#endif
    }
    void cleanupTestCase() {
        if (previousSettingsSet_)
            setSettingsOverride(previousSettings_);
        else
            qunsetenv("SEVENZIP_SETTINGS_PATH");
    }
    void formatOrderAndPriority_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("saved");
        QTest::addColumn<QString>("explicitFormat");
        QTest::addColumn<bool>("reverse");
        for (const QString &language : {QString("-"), QString("ru")}) {
            for (bool reverse : {false, true}) {
                const QString tag = language + QString::number(reverse);
                QTest::newRow(qPrintable("saved-" + tag))
                    << language << QString("zip") << QString() << reverse;
                QTest::newRow(qPrintable("explicit-" + tag))
                    << language << QString("7z") << QString("zip") << reverse;
                QTest::newRow(qPrintable("fallback-" + tag))
                    << language << QString("missing-handler") << QString() << reverse;
            }
        }
    }
    void formatOrderAndPriority() {
        QFETCH(QString, language);
        QFETCH(QString, saved);
        QFETCH(QString, explicitFormat);
        QFETCH(bool, reverse);
        QFile::remove(settings_);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        NCompression::CInfo registry;
        registry.ArcType = saved;
        registry.Save(settings_);
        QFile settingsFile(settings_);
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        const QByteArray before = settingsFile.readAll();
        settingsFile.close();
        CCompressDialog source;
        source.Info.FormatIndex =
            explicitFormat.isEmpty() ? -1 : catalog_->formatIndex(explicitFormat);
        source.Info.ArcPath = fixture_.filePath("chosen").toStdWString().c_str();
        CObjectVector<NWildcard::CCensorPath> paths;
        paths.AddNew().Path = input_.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog_->nativeCodecs(), paths),
                 S_OK);
        // Native order supplies fallback; CBS_SORT controls only presentation.
        if (reverse)
            for (unsigned i = 0; i < source.ArcIndices.Size() / 2; ++i)
                std::swap(source.ArcIndices[i],
                          source.ArcIndices[source.ArcIndices.Size() - i - 1]);
        const QString fallback =
            QString::fromWCharArray(catalog_->nativeCodecs().Formats[source.ArcIndices[0]].Name)
                .toLower();
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_, &source);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        QVERIFY(format);
        const QString expected = explicitFormat.isEmpty()
                                     ? (saved == "missing-handler" ? fallback : saved)
                                     : explicitFormat;
        QCOMPARE(format->currentText(), expected);
        QStringList actual;
        for (int i = 0; i < format->count(); ++i)
            actual.append(format->itemText(i));
        QStringList sorted = actual;
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(sorted.begin(), sorted.end(),
                         [&](const QString &a, const QString &b) { return collator(a, b); });
        QCOMPARE(actual, sorted);
        for (int i = 0; i < format->count(); ++i) {
            format->setCurrentIndex(i);
            QCOMPARE(dialog.sourceInfo().FormatIndex, catalog_->formatIndex(format->itemText(i)));
        }
        format->setCurrentText(expected);
        dialog.show();
        QApplication::processEvents();
        for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
            dialog.resize(size);
            QApplication::processEvents();
            dialog.findChild<QScrollArea *>()->ensureWidgetVisible(format);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*format, dialog));
            QVERIFY(UiInspection::writeReport(dialog,
                                              QString("format-order-%1-%2")
                                                  .arg(QString::fromLatin1(QTest::currentDataTag()))
                                                  .arg(size.width())));
        }
        dialog.reject();
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        QCOMPARE(settingsFile.readAll(), before);
    }
    void browseUsesDisplayedFormatIndices() {
        CCompressDialog source;
        source.ArcFormats = &catalog_->nativeCodecs().Formats;
        source.Info.FormatIndex = catalog_->formatIndex("zip");
        for (int i = int(source.ArcFormats->Size()) - 1; i >= 0; --i)
            if ((*source.ArcFormats)[unsigned(i)].UpdateEnabled)
                source.ArcIndices.Add(unsigned(i));
        QComboBox control;
        NCompression::CInfo registry;
        QVERIFY(source.OnInit(control, registry));
        QCOMPARE(control.currentData().toInt(), catalog_->formatIndex("zip"));
        source.Format = control.currentText();
        source.DirPrefix = fixture_.path();
        source.m_ArchivePath = "chosen.zip";
        const QString previous = source.m_ArchivePath;
        int calls = 0;
        int filterIndex = -1;
        CObjectVector<CBrowseFilterInfo> displayed;
        QVERIFY(!source.OnButtonSetArchive(
            nullptr, [&](CBrowseInfo &info, const CObjectVector<CBrowseFilterInfo> &filters) {
                ++calls;
                filterIndex = info.FilterIndex;
                displayed = filters;
                return false;
            }));
        QCOMPARE(calls, 1);
        QCOMPARE(filterIndex, control.currentIndex());
        QCOMPARE(displayed.Size(), unsigned(control.count() + 2));
        for (int i = 0; i < control.count(); ++i) {
            QCOMPARE(source.ArcIndices[unsigned(i)], control.itemData(i).toUInt());
            const auto &format = (*source.ArcFormats)[source.ArcIndices[unsigned(i)]];
            QVERIFY(displayed[unsigned(i)].Description.IsPrefixedBy(format.Name));
        }
        QCOMPARE(source.m_ArchivePath, previous);
        QCOMPARE(source.Info.FormatIndex, catalog_->formatIndex("zip"));
        // Returning a different sorted filter must publish its native handler,
        // extension and path together, rather than the old registration index.
        const int gzip = control.findData(unsigned(catalog_->formatIndex("gzip")));
        QVERIFY(gzip >= 0);
        QVERIFY(source.OnButtonSetArchive(
            nullptr, [&](CBrowseInfo &info, const CObjectVector<CBrowseFilterInfo> &) {
                info.FilterIndex = gzip;
                info.FilePath = ToUString(fixture_.filePath("browse 空 with spaces"));
                return true;
            }));
        QCOMPARE(source.Info.FormatIndex, catalog_->formatIndex("gzip"));
        QCOMPARE(source.Format, QString("gzip"));
        QVERIFY(source.m_ArchivePath.endsWith(".gz"));
    }
    void sourceLevelSpecKeepsAutoSentinel() {
        QComboBox control;
        QCOMPARE(CCompressDialog::GetComboValue(control), UInt32(-1));
        control.addItem("0", 0u);
        QCOMPARE(CCompressDialog::GetComboValue(control), 0u);
        QCOMPARE(CCompressDialog::GetComboValue(control, 1), UInt32(-1));
        control.addItem("5", 5u);
        control.setCurrentIndex(-1);
        QCOMPARE(CCompressDialog::GetComboValue(control, 1), UInt32(-1));
        control.setCurrentIndex(1);
        QCOMPARE(CCompressDialog::GetComboValue(control, 1), 5u);
        QFile::remove(settings_);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("tar");
        QCOMPARE(dialog.compressionOptions().Level, 0u);
        QCOMPARE(dialog.sourceInfo().Level, UInt32(-1));
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        NCompression::CInfo registry;
        registry.Load(settings_);
        const unsigned index = CCompressDialog::FindRegistryFormat_Always(registry, "tar");
        QCOMPARE(registry.Formats[index].Level, UInt32(-1));
    }
    void acceptedFormatSurvivesRestartAndCreatesArchive() {
        QFile::remove(settings_);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("zip");
        dialog.findChild<QLineEdit *>("archiveName")->setText("accepted 空 with spaces.zip");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        CreateArchiveDialog restarted(fixture_.path(), {input_}, nullptr, catalog_);
        QCOMPARE(restarted.findChild<QComboBox *>("archiveFormat")->currentText(), QString("zip"));
        const auto options = restarted.compressionOptions();
        restarted.reject();
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        QCOMPARE(
            SourceCreationFixture::Create(backend, dialog.target(), {input_}, {}, false, options),
            S_OK);
        QList<ArchiveEntry> entries;
        connect(&backend, &ArchiveBackend::entriesReady, this,
                [&](const QList<ArchiveEntry> &value) { entries = value; });
        done.clear();
        backend.list(dialog.target());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done.first()[0].toBool());
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().size, qint64(22));
        done.clear();
        backend.test(dialog.target());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done.first()[0].toBool());
        done.clear();
        const QString output = fixture_.filePath("output 空 with spaces");
        backend.extract(dialog.target(), output);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY(done.first()[0].toBool());
        QFile extracted(QDir(output).filePath(entries.first().path));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("format selection bytes"));
        auto invalid = options;
        invalid.NumThreads = 999999;
        done.clear();
        const QString failed = fixture_.filePath("failed.zip");
        QVERIFY(SourceCreationFixture::Create(backend, failed, {input_}, {}, false, invalid) !=
                S_OK);
        QVERIFY(!QFileInfo::exists(failed));
        done.clear();
        const QString cancelled = fixture_.filePath("cancelled.zip");
        QCOMPARE(
            SourceCreationFixture::Create(backend, cancelled, {input_}, {}, false, options, true),
            E_ABORT);
        QVERIFY(!QFileInfo::exists(cancelled));
    }
    void hashMethodUpdatesArchiveName_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void hashMethodUpdatesArchiveName() {
        QFETCH(QString, language);
        QFile::remove(settings_);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        const int hash = catalog_->formatIndex("hash");
        QVERIFY(hash >= 0);
        CCompressDialog source;
        source.Info.FormatIndex = hash;
        source.Info.ArcPath = fixture_.filePath("hash output 空").toStdWString().c_str();
        CObjectVector<NWildcard::CCensorPath> paths;
        paths.AddNew().Path = input_.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog_->nativeCodecs(), paths),
                 S_OK);
        CreateArchiveDialog dialog(fixture_.path(), {input_}, nullptr, catalog_, &source);
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        QCOMPARE(method->currentText(), QString("*  SHA256"));
        QCOMPARE(dialog.sourceInfo().Level, UInt32(-1));
        QVERIFY(name->text().endsWith(".sha256"));
        method->setCurrentText("SHA1");
        QCOMPARE(QString::fromWCharArray(dialog.sourceInfo().Method.Ptr()), QString("SHA1"));
        QVERIFY2(name->text().endsWith(".sha1"), qPrintable(name->text()));
        method->setCurrentIndex(0);
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        QVERIFY(name->text().endsWith(".sha256"));
        dialog.reject();
    }
    void nativeHashModalWritesChosenMethod() {
        QFile::remove(settings_);
        QFile::remove(input_ + ".sha1");
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, catalog_->version()), S_OK);
        QStringList errors;
        QtShellArchiveServices service(
            backend, nullptr,
            {.Error =
                 [&](HRESULT, const UString &message) {
                     errors.append(QString::fromWCharArray(message.Ptr()));
                 },
             .Modal =
                 [&](CCompressDialog &source, const CObjectVector<NWildcard::CCensorPath> &paths,
                     QWidget *parent) {
                     return CreateArchiveDialog::PresentSource(source, paths,
                                                               backend.codecCatalog(), parent);
                 }});
        bool shown = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *dialog =
                    qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget())) {
                if (!shown) {
                    shown = true;
                    dialog->findChild<QComboBox *>("compressionMethod")->setCurrentText("SHA1");
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                }
            } else if (auto *progress =
                           dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && button->property("languageID").toUInt() == IDS_CLOSE)
                    button->click();
            } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            }
        });
        responder.start(5);
        UStringVector paths;
        paths.Add(UString(input_.toStdWString().c_str()));
        service.CompressFiles(UString((fixture_.path() + '/').toStdWString().c_str()),
                              UString(L"checksums.sha256"), UString(L"hash"), false, paths, false,
                              true, false);
        responder.stop();
        QVERIFY(shown);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(" | ")));
        QCOMPARE(service.Result(), S_OK);
        QFile manifest(input_ + ".sha1");
        QVERIFY(manifest.open(QIODevice::ReadOnly));
        const QByteArray bytes = manifest.readAll();
        QVERIFY(bytes.toLower().contains(
            QCryptographicHash::hash("format selection bytes", QCryptographicHash::Sha1).toHex()));
        QVERIFY(
            QString::fromUtf8(bytes)
                .normalized(QString::NormalizationForm_C)
                .contains(QFileInfo(input_).fileName().normalized(QString::NormalizationForm_C)));
    }
};
QTEST_MAIN(CompressionFormatTests)
#include "compression_format_tests.moc"
