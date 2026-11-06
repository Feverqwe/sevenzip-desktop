#include "7zip/PropID.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "support/archive_expert.h"
#include "support/archive_sections.h"
#include "ui/support/layout_inspection.h"
#include <QDateTime>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimeZone>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QCheckBox *check(QWidget &dialog, const char *name) { return dialog.findChild<QCheckBox *>(name); }
CCompressDialog owner(NCompression::CFormatOptions &profile, const QString &format) {
    CCompressDialog result;
    result.FormatOptions = &profile;
    result.ArcInfo.Name = format.toStdWString().c_str();
    result.ArcInfo.Flags = NArcInfoFlags::kCTime | NArcInfoFlags::kATime | NArcInfoFlags::kMTime |
                           NArcInfoFlags::kMTime_Default;
    result.ArcInfo.TimeFlags = TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kWindows);
    if (format == "zip")
        result.ArcInfo.TimeFlags |= TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kUnix) |
                                    TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kDOS);
    return result;
}
} // namespace

class CompressionAdvancedTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory_;
    QByteArray oldSettings_;
    QString settings_;

  private slots:
    void helpUsesSourceOptionsTopicWithoutApplyingPendingState() {
        NCompression::CFormatOptions profile;
        auto compression = owner(profile, "zip");
        COptionsDialog dialog(&compression);
        dialog.show();
        QApplication::processEvents();
        const bool defined = profile.MTime.Def;
        const bool value = profile.MTime.Val;
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons && buttons->button(QDialogButtonBox::Help)->isEnabled());
        buttons->button(QDialogButtonBox::Help)->click();
        auto *help = dialog.findChild<QDialog *>("helpWindow");
        QVERIFY(help && help->isVisible());
        auto *browser = help->findChild<QTextBrowser *>("helpBrowser");
        QCOMPARE(browser->source().path(), QString("/upstream/7zip/Help/fm/plugins/7-zip/add.htm"));
        QCOMPARE(browser->source().fragment(), QString("options"));
        QCOMPARE(profile.MTime.Def, defined);
        QCOMPARE(profile.MTime.Val, value);
        help->close();
        dialog.reject();
    }
    void initTestCase() {
        QVERIFY(settingsDirectory_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = settingsDirectory_.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
    }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void init() { QFile::remove(settings_); }
    void optionsStatesAndGeometry_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QSize>("size");
        QTest::newRow("zip-normal") << QString("zip") << QSize(520, 400);
        QTest::newRow("zip-compact") << QString("zip") << QSize(440, 340);
        QTest::newRow("7z-normal") << QString("7z") << QSize(520, 400);
        QTest::newRow("7z-compact") << QString("7z") << QSize(440, 340);
    }
    void optionsStatesAndGeometry() {
        QFETCH(QString, format);
        QFETCH(QSize, size);
        NCompression::CFormatOptions profile;
        auto source = owner(profile, format);
        COptionsDialog dialog(&source);
        dialog.resize(size);
        dialog.show();
        QCoreApplication::processEvents();
        QCOMPARE(dialog.size(), size.expandedTo(dialog.minimumSizeHint()));
        auto *precision = dialog.findChild<QComboBox *>("IDC_COMPRESS_TIME_PREC");
        QCOMPARE(precision->count(), format == "zip" ? 3 : 1);
        QVERIFY(!dialog.findChild<QGroupBox *>("IDG_COMPRESS_NTFS")->isVisible());
        QVERIFY(check(dialog, "IDX_COMPRESS_MTIME")->isChecked());
        QVERIFY(!check(dialog, "IDX_COMPRESS_MTIME")->isEnabled());
        QVERIFY(!check(dialog, "IDX_COMPRESS_MTIME_SET")->isVisible());
        QVERIFY(!check(dialog, "IDX_COMPRESS_CTIME")->isEnabled());
#ifdef Q_OS_WIN
        QVERIFY(check(dialog, "IDX_COMPRESS_PRESERVE_ATIME")->isEnabled());
#else
        QVERIFY(!check(dialog, "IDX_COMPRESS_PRESERVE_ATIME")->isEnabled());
#endif
        QVERIFY(check(dialog, "IDX_COMPRESS_ZTIME_SET")->isEnabled());
        if (format == "zip") {
            check(dialog, "IDX_COMPRESS_PREC_SET")->click();
            precision->setCurrentIndex(precision->findData(UInt32(NFileTimeType::kUnix)));
            QMetaObject::invokeMethod(precision, "activated", Qt::DirectConnection,
                                      Q_ARG(int, precision->currentIndex()));
            QVERIFY(!check(dialog, "IDX_COMPRESS_CTIME")->isVisible());
            QVERIFY(!check(dialog, "IDX_COMPRESS_ATIME")->isVisible());
            check(dialog, "IDX_COMPRESS_PREC_SET")->click();
            QCOMPARE(precision->currentData().toUInt(), UInt32(NFileTimeType::kWindows));
            QVERIFY(check(dialog, "IDX_COMPRESS_CTIME")->isVisible());
        }
        check(dialog, "IDX_COMPRESS_CTIME_SET")->click();
        QVERIFY(check(dialog, "IDX_COMPRESS_CTIME")->isEnabled());
        check(dialog, "IDX_COMPRESS_CTIME")->click();
        auto *buttons = dialog.findChild<QDialogButtonBox *>("compressOptionsButtons");
        for (QWidget *widget : {static_cast<QWidget *>(precision),
                                static_cast<QWidget *>(check(dialog, "IDX_COMPRESS_CTIME")),
                                static_cast<QWidget *>(buttons)})
            QVERIFY(UiInspection::fullyVisible(*widget, dialog));
#ifdef Q_OS_WIN
        QVERIFY(UiInspection::fullyVisible(*check(dialog, "IDX_COMPRESS_PRESERVE_ATIME"), dialog));
#else
        QVERIFY(check(dialog, "IDX_COMPRESS_PRESERVE_ATIME")->isHidden());
#endif
        QVERIFY(UiInspection::separate(*dialog.findChild<QGroupBox *>("IDG_COMPRESS_TIME"),
                                       *buttons, dialog));
        dialog.resize(dialog.width(), dialog.minimumSizeHint().height());
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::writeReport(
            dialog, QString("compression-options-%1-%2").arg(format).arg(size.width())));
        dialog.accept();
        QVERIFY(profile.CTime.Def);
        QVERIFY(profile.CTime.Val);
        QVERIFY(!profile.SetArcMTime.Def);
    }
    void lifecycleRoundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("volumes");
        QTest::newRow("7z") << QString("7z") << false;
        QTest::newRow("zip") << QString("zip") << false;
        QTest::newRow("7z-volumes") << QString("7z") << true;
        QTest::newRow("zip-volumes") << QString("zip") << true;
    }
    void lifecycleRoundTrip() {
        QFETCH(QString, format);
        QFETCH(bool, volumes);
        QTemporaryDir first, second, output;
        QVERIFY(first.isValid() && second.isValid() && output.isValid());
        const QString a = first.filePath(QStringLiteral("первый файл.txt"));
        const QString b = second.filePath(QStringLiteral("второй файл.txt"));
        QVERIFY(writeFile(a, QByteArray(4096, 'a')));
        QVERIFY(writeFile(b, QByteArray(4096, 'b')));
        const QDateTime latest = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone(0));
        const QDateTime accessed = latest.addDays(-5);
        for (const QString &path : {a, b}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(path == a ? latest : latest.addDays(-1),
                                     QFileDevice::FileModificationTime));
            QVERIFY(file.setFileTime(accessed, QFileDevice::FileAccessTime));
        }
        CompressionOptions options;
        options.Level = 0;
        options.SetArcMTime.SetTrueTrue();
#ifdef Q_OS_WIN
        options.PreserveATime.SetTrueTrue();
#endif
        options.OpenShareForWrite = true;
        if (volumes)
            options.VolumeSizes.Add(2048);
        const QString archive = output.filePath("result." + format);
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {a, b}, {}, false, options), S_OK);
#ifdef Q_OS_WIN
        for (const QString &path : {a, b})
            QCOMPARE(QFileInfo(path).lastRead(), accessed);
#endif
        const QStringList files = QDir(output.path()).entryList(QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString &name : files)
            QCOMPARE(QFileInfo(output.filePath(name)).lastModified(), latest);
    }
    void unsupportedPreserveAccessTime() {
#ifndef Q_OS_WIN
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("input.txt");
        QVERIFY(writeFile(source, "retained source"));
        CompressionOptions options;
        options.PreserveATime.SetTrueTrue();
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QVERIFY(SourceCreationFixture::Create(backend, fixture.filePath("result.7z"), {source}, {},
                                              false, options) != S_OK);
        QCOMPARE(readFile(source), QByteArray("retained source"));
        QVERIFY(!QFileInfo::exists(fixture.filePath("result.7z")));
        NCompression::CInfo persisted;
        persisted.PreserveATime.SetTrueTrue();
        persisted.Save(settings_);
        CreateArchiveDialog dialog(fixture.path(), {source});
        QVERIFY(!dialog.compressionOptions().PreserveATime.Val);
        NCompression::CInfo loaded;
        loaded.Load(settings_);
        QVERIFY(loaded.PreserveATime.Def && loaded.PreserveATime.Val);
#endif
    }
    void addFormGeometry_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(760, 720);
        QTest::newRow("compact") << QSize(640, 480);
    }
    void lifecycleFormGeometry_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(760, 720);
        QTest::newRow("compact") << QSize(640, 480);
    }
    void lifecycleFormGeometry() {
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        QVERIFY(writeFile(source, "source bytes"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        dialog.resize(size);
        dialog.show();
        ExpandArchiveSections(dialog);
        QCoreApplication::processEvents();
        const bool edited = EditArchiveExpert(dialog, true, [&](QDialog &editor) {
            editor.resize(size);
            auto *group = dialog.findChild<QWidget *>("archiveLifecycleOptions");
            auto *shared = check(dialog, "archiveSharedFiles");
            auto *remove = check(dialog, "archiveDeleteAfterCompression");
            QVERIFY(group && shared && remove);
            auto *scroll = editor.findChild<QScrollArea *>("dialogContentScroll");
            scroll->ensureWidgetVisible(group);
            QCoreApplication::processEvents();
            for (QWidget *widget : {static_cast<QWidget *>(group), static_cast<QWidget *>(remove)})
                QVERIFY(UiInspection::fullyVisible(*widget, editor));
            QVERIFY(remove->width() >= remove->sizeHint().width());
#ifdef Q_OS_WIN
            QVERIFY(UiInspection::fullyVisible(*shared, editor));
            QVERIFY(shared->width() >= shared->sizeHint().width());
            QVERIFY(UiInspection::bounds(*shared, editor).bottom() <
                    UiInspection::bounds(*remove, editor).top());
            QVERIFY(shared->isEnabled());
            shared->setChecked(true);
            QVERIFY(dialog.compressionOptions().OpenShareForWrite);
#else
        QVERIFY(!shared->isVisible());
        QVERIFY(!shared->isEnabled());
        QVERIFY(!shared->toolTip().isEmpty());
        QVERIFY(!dialog.compressionOptions().OpenShareForWrite);
#endif
            QVERIFY(remove->isEnabled());
            remove->setChecked(true);
            QVERIFY(dialog.compressionOptions().DeleteAfterCompressing);
            auto *mode = dialog.findChild<QComboBox *>("archiveUpdateMode");
            mode->setCurrentIndex(mode->findData(int(NCompressDialog::NUpdateMode::kFresh)));
            QVERIFY(remove->isEnabled());
            QVERIFY(dialog.compressionOptions().DeleteAfterCompressing);
            mode->setCurrentIndex(mode->findData(int(NCompressDialog::NUpdateMode::kAdd)));
            QVERIFY(remove->isEnabled());
            QVERIFY(dialog.compressionOptions().DeleteAfterCompressing);
            QVERIFY(UiInspection::writeReport(editor,
                                              QString("archive-lifecycle-%1").arg(size.width())));
        });
        QVERIFY(edited);
        dialog.reject();
        CreateArchiveDialog reopened(fixture.path(), {source});
        QVERIFY(!reopened.compressionOptions().DeleteAfterCompressing);
    }
    void addFormGeometry() {
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        QVERIFY(writeFile(source, "data"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        dialog.resize(size);
        dialog.show();
        ExpandArchiveSections(dialog);
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        scroll->ensureWidgetVisible(volume);
        QTRY_VERIFY(UiInspection::fullyVisible(*volume, dialog));
        QVERIFY(UiInspection::writeReport(dialog, QString("archive-volumes-%1").arg(size.width())));
        QVERIFY(EditArchiveExpert(dialog, false, [&](QDialog &editor) {
            editor.resize(size);
            auto *parameters = editor.findChild<QLineEdit *>("archiveParameters");
            auto *advanced = editor.findChild<QPushButton *>("archiveAdvancedOptions");
            auto *scroll = editor.findChild<QScrollArea *>("dialogContentScroll");
            auto *buttons = editor.findChild<QDialogButtonBox *>();
            scroll->ensureWidgetVisible(parameters);
            QTRY_VERIFY(UiInspection::fullyVisible(*parameters, editor));
            QVERIFY(UiInspection::writeReport(editor,
                                              QString("archive-parameters-%1").arg(size.width())));
            scroll->ensureWidgetVisible(advanced);
            QTRY_VERIFY(UiInspection::fullyVisible(*advanced, editor));
            QVERIFY(UiInspection::fullyVisible(*buttons, editor));
            QVERIFY(UiInspection::separate(*parameters, *advanced, editor));
            QVERIFY(UiInspection::writeReport(editor,
                                              QString("archive-advanced-%1").arg(size.width())));
        }));
    }

    void sourceMetadataPriority_data() {
        QTest::addColumn<bool>("stored");
        QTest::addColumn<bool>("defined");
        QTest::addColumn<bool>("requested");
        QTest::addColumn<bool>("expected");
        QTest::newRow("explicit-off") << true << true << false << false;
        QTest::newRow("explicit-on") << false << true << true << true;
        QTest::newRow("saved-default") << true << false << false << true;
    }
    void sourceMetadataPriority() {
        QFETCH(bool, stored);
        QFETCH(bool, defined);
        QFETCH(bool, requested);
        QFETCH(bool, expected);
        const auto catalog = NativeArchiveFixture::Catalog();
        QVERIFY(catalog);
        QCOMPARE(catalog->result(), S_OK);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("source.txt");
        QVERIFY(writeFile(input, "data"));
        NCompression::CInfo registry;
        registry.SymLinks.Def = true;
        registry.SymLinks.Val = stored;
        registry.Save(settings_);
        const QByteArray before = readFile(settings_);
        CCompressDialog source;
        source.Info.FormatIndex = catalog->formatIndex("tar");
        QVERIFY(source.Info.FormatIndex >= 0);
        source.Info.ArcPath = fixture.filePath("output.tar").toStdWString().c_str();
        source.Info.SymLinks.Def = defined;
        source.Info.SymLinks.Val = requested;
        CObjectVector<NWildcard::CCensorPath> paths;
        paths.AddNew().Path = input.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog->nativeCodecs(), paths),
                 S_OK);
        CreateArchiveDialog form(fixture.path(), {input}, nullptr, catalog, &source);
        QCOMPARE(form.findChild<QComboBox *>("archiveFormat")->currentText(), QString("tar"));
        QCOMPARE(form.compressionOptions().SymLinks.Val, expected);
        bool inspected = false;
        QTimer::singleShot(0, &form, [&] {
            auto *options = dynamic_cast<COptionsDialog *>(QApplication::activeModalWidget());
            QVERIFY(options);
            const auto cancel = qScopeGuard([&] { options->reject(); });
            auto *links = check(*options, "IDX_COMPRESS_NT_SYM_LINKS");
            QVERIFY(links && links->isEnabled() && links->isVisible());
            QCOMPARE(links->isChecked(), expected);
            links->setChecked(!expected);
            inspected = true;
        });
        form.findChild<QPushButton *>("archiveAdvancedOptions")->click();
        QVERIFY(inspected);
        QCOMPARE(form.compressionOptions().SymLinks.Val, expected);
        form.reject();
        QCOMPARE(readFile(settings_), before);
    }
    void profilesAndCancel() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source.txt");
        QVERIFY(writeFile(source, "data"));
        CreateArchiveDialog dialog(fixture.path(), {source});
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        auto *advanced = dialog.findChild<QPushButton *>("archiveAdvancedOptions");
        parameters->setText("  -mtm=off mt=2  ");
        format->setCurrentText("zip");
        QVERIFY(parameters->text().isEmpty());
        parameters->setText("m=Store");
        format->setCurrentText("7z");
        QCOMPARE(parameters->text(), QString("-mtm=off mt=2"));
        const auto setTime = [&](bool accept) {
            QTimer::singleShot(0, [&] {
                auto *options = dynamic_cast<COptionsDialog *>(QApplication::activeModalWidget());
                QVERIFY(options);
                check(*options, "IDX_COMPRESS_CTIME_SET")->click();
                check(*options, "IDX_COMPRESS_CTIME")->click();
                if (accept)
                    options->accept();
                else
                    options->reject();
            });
            advanced->click();
        };
        setTime(false);
        QVERIFY(!dialog.compressionOptions().CTime.Def);
        setTime(true);
        QVERIFY(dialog.compressionOptions().CTime.Val);
        QVERIFY(!QFileInfo::exists(settings_));
        dialog.reject();
        QVERIFY(!QFileInfo::exists(settings_));
        // A separate confirmed Add saves both per-format property sets.
        CreateArchiveDialog confirmed(fixture.path(), {source});
        QTimer::singleShot(0, [&] {
            auto *options = dynamic_cast<COptionsDialog *>(QApplication::activeModalWidget());
            QVERIFY(options);
            check(*options, "IDX_COMPRESS_CTIME_SET")->click();
            check(*options, "IDX_COMPRESS_CTIME")->click();
#ifdef Q_OS_WIN
            check(*options, "IDX_COMPRESS_PRESERVE_ATIME")->click();
#endif
            check(*options, "IDX_COMPRESS_ZTIME_SET")->click();
            check(*options, "IDX_COMPRESS_ZTIME")->click();
            options->accept();
        });
        confirmed.findChild<QPushButton *>("archiveAdvancedOptions")->click();
        confirmed.findChild<QLineEdit *>("archiveParameters")->setText("mt=2");
        confirmed.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        confirmed.findChild<QLineEdit *>("archiveParameters")->setText("m=Store");
        QTimer::singleShot(0, [&] {
            auto *options = dynamic_cast<COptionsDialog *>(QApplication::activeModalWidget());
            QVERIFY(options);
            check(*options, "IDX_COMPRESS_PREC_SET")->click();
            auto *precision = options->findChild<QComboBox *>("IDC_COMPRESS_TIME_PREC");
            precision->setCurrentIndex(precision->findData(UInt32(NFileTimeType::kUnix)));
            options->accept();
        });
        confirmed.findChild<QPushButton *>("archiveAdvancedOptions")->click();
        confirmed.findChild<QComboBox *>("archiveFormat")->setCurrentText("7z");
        confirmed.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(confirmed.result(), int(QDialog::Accepted));
        CreateArchiveDialog reopened(fixture.path(), {source});
        QCOMPARE(reopened.findChild<QLineEdit *>("archiveParameters")->text(), QString("mt=2"));
        QVERIFY(reopened.compressionOptions().CTime.Def);
        QVERIFY(reopened.compressionOptions().CTime.Val);
#ifdef Q_OS_WIN
        QVERIFY(reopened.compressionOptions().PreserveATime.Def);
        QVERIFY(reopened.compressionOptions().PreserveATime.Val);
#endif
        QVERIFY(reopened.compressionOptions().SetArcMTime.Val);
        reopened.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        QCOMPARE(reopened.findChild<QLineEdit *>("archiveParameters")->text(), QString("m=Store"));
        QCOMPARE(reopened.compressionOptions().TimePrec, UInt32(NFileTimeType::kUnix));
    }
    void timestampAndMethodRoundTrip_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<UInt32>("precision");
        QTest::newRow("7z-windows") << QString("7z") << UInt32(NFileTimeType::kWindows);
        QTest::newRow("zip-windows") << QString("zip") << UInt32(NFileTimeType::kWindows);
        QTest::newRow("zip-unix") << QString("zip") << UInt32(NFileTimeType::kUnix);
        QTest::newRow("zip-dos") << QString("zip") << UInt32(NFileTimeType::kDOS);
    }
    void timestampAndMethodRoundTrip() {
        QFETCH(QString, format);
        QFETCH(UInt32, precision);
        QTemporaryDir fixture, output;
        QVERIFY(fixture.isValid());
        QVERIFY(output.isValid());
        const QString name = QStringLiteral("время с пробелом.txt");
        const QString source = fixture.filePath(name);
        QVERIFY(writeFile(source, QByteArray(8192, 'a')));
        QFile file(source);
        QVERIFY(file.open(QIODevice::ReadWrite));
        const qint64 time = 1700000001123;
        QVERIFY(file.setFileTime(QDateTime::fromMSecsSinceEpoch(time, QTimeZone(0)),
                                 QFileDevice::FileModificationTime));
        file.close();
        CompressionOptions options;
        options.TimePrec = precision;
        options.Options = format == "7z" ? "-m0=BZip2" : "-mm=Store";
        const QString archive = fixture.filePath("time." + format);
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, archive, {source}, {}, false, options),
                 S_OK);
        QProcess listing;
        listing.start("7z", {"l", "-slt", "--", archive});
        QVERIFY(listing.waitForFinished(15000));
        QCOMPARE(listing.exitCode(), 0);
        const QByteArray metadata = listing.readAllStandardOutput();
        QVERIFY(metadata.contains(format == "7z" ? "Method = BZip2" : "Method = Store"));
        QSignalSpy opened(&backend, &ArchiveBackend::entriesReady);
        backend.list(archive);
        QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 15000);
        finished.clear();
        backend.extract(archive, output.path());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QVERIFY2(finished.last().first().toBool(), qPrintable(finished.last().at(1).toString()));
        const qint64 actual = QFileInfo(output.filePath(name)).lastModified().toMSecsSinceEpoch();
        if (precision == NFileTimeType::kDOS) {
            // Source TimeUtils rounds up on POSIX; Windows uses its native API.
            // Both must store two-second precision within one quantum.
            QCOMPARE(actual % 2000, qint64(0));
            QVERIFY(qAbs(actual - time) < 2000);
#ifndef Q_OS_WIN
            QCOMPARE(actual, (time + 1999) / 2000 * 2000);
#endif
        } else
            QCOMPARE(actual, precision == NFileTimeType::kUnix ? time / 1000 * 1000 : time);
        QCOMPARE(readFile(output.filePath(name)), QByteArray(8192, 'a'));
    }
    void omittedTimeInvalidPropertyAndCancel() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("исходник с пробелом.txt"));
        const QByteArray bytes(500000, 'a');
        QVERIFY(writeFile(source, bytes));
        CompressionOptions options;
        options.MTime.Def = true;
        options.MTime.Val = false;
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        const QString target = fixture.filePath("no-time.7z");
        QCOMPARE(SourceCreationFixture::Create(backend, target, {source}, {}, false, options),
                 S_OK);
        QSignalSpy listed(&backend, &ArchiveBackend::entriesReady);
        backend.list(target);
        QTRY_COMPARE_WITH_TIMEOUT(listed.size(), 1, 15000);
        const auto entries = qvariant_cast<QList<ArchiveEntry>>(listed.first().first());
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().modified.isEmpty());
        options.Options = "unknown-property=on";
        const QString bad = fixture.filePath("invalid.7z");
        finished.clear();
        QVERIFY(SourceCreationFixture::Create(backend, bad, {source}, {}, false, options) != S_OK);
        QVERIFY(!QFileInfo::exists(bad));
        options.Options.Empty();
        options.TimePrec = UInt32(NFileTimeType::kUnix); // Unsupported by the source 7z handler.
        finished.clear();
        QVERIFY(SourceCreationFixture::Create(backend, bad, {source}, {}, false, options) != S_OK);
        QVERIFY(!QFileInfo::exists(bad));
        options.TimePrec = UInt32(-1);
        options.Options = "0=BZip2";
        finished.clear();
        QCOMPARE(SourceCreationFixture::Create(backend, bad, {source}, {}, false, options, true),
                 E_ABORT);
        QVERIFY(!QFileInfo::exists(bad));
        QCOMPARE(readFile(source), bytes);
        QVERIFY(
            QDir(fixture.path())
                .entryList({".sevenzip-*"}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CompressionAdvancedTests)
#include "compression_advanced_tests.moc"
