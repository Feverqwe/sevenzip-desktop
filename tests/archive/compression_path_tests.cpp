#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_creation_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/UpdateGUI.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class CompressionPathTests final : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QByteArray previousSettings_;
    QString settings_;
    QString source_;

    static void submit(CreateArchiveDialog &dialog) {
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    }

  private slots:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = dir_.filePath("preferences.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
        source_ = dir_.filePath(QStringLiteral("источник с пробелом.txt"));
        QFile file(source_);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("archive path regression"), qint64(23));
    }
    void init() { QFile::remove(settings_); }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void namesAndFormats() {
        CCompressDialog path;
        path.SetArchiveName("report.tar.gz");
        QCOMPARE(path.m_ArchivePath, QString("report.tar.7z"));
        path.Format = "zip";
        path.SetArchiveName2();
        QCOMPARE(path.m_ArchivePath, QString("report.tar.zip"));
        path.SetArchiveName(".hidden");
        QCOMPARE(path.m_ArchivePath, QString(".hidden.zip"));
        path.SetArchiveName("folder.with.dot/plain");
        QCOMPARE(path.m_ArchivePath, QString("folder.with.dot/plain.zip"));
        path.SetArchiveName(QStringLiteral("папка/.скрытый"));
        QCOMPARE(path.m_ArchivePath, QStringLiteral("папка/.скрытый.zip"));
        path.ArcPath_WasChanged("NAME.7Z");
        QCOMPARE(path.Format, QString("7z"));
        path.ArcPath_WasChanged("unknown.rar");
        QCOMPARE(path.Format, QString("7z"));
        path.ArcPath_WasChanged(".zip");
        QCOMPARE(path.Format, QString("7z"));
    }
    void nativeMetadataKeepsOriginalNamesAndUsesMainExtension() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; native naming unverified");
#else
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        CCompressDialog path;
        path.ArcFormats = &catalog->nativeCodecs().Formats;
        path.ArcIndices.Add(unsigned(catalog->formatIndex("7z")));
        path.ArcIndices.Add(unsigned(catalog->formatIndex("zip")));
        path.Info.FormatIndex = catalog->formatIndex("gzip");
        path.OriginalFileName = L"исходное 空.tar";
        path.SetArchiveName("ignored name.zip");
        QCOMPARE(path.m_ArchivePath, QStringLiteral("исходное 空.tar.gz"));
        path.Info.FormatIndex = catalog->formatIndex("zip");
        path.SetArchiveName2();
        QCOMPARE(path.m_ArchivePath, QStringLiteral("исходное 空.zip"));
        path.Info.KeepName = true;
        path.Info.FormatIndex = catalog->formatIndex("gzip");
        path.SetArchiveName("ignored again");
        path.Info.FormatIndex = catalog->formatIndex("7z");
        path.SetArchiveName2();
        QCOMPARE(path.m_ArchivePath, QStringLiteral("исходное 空.tar.7z"));
        path.SetArchiveName2();
        QCOMPARE(path.m_ArchivePath, QStringLiteral("исходное 空.tar.7z"));
        path.ArcPath_WasChanged("document.DOCX");
        QCOMPARE(path.Info.FormatIndex, catalog->formatIndex("zip"));
        QCOMPARE(path.Format, QStringLiteral("zip"));
        path.ArcPath_WasChanged("document.rar");
        QCOMPARE(path.Info.FormatIndex, catalog->formatIndex("zip"));
#endif
    }
    void sourceSelectionControlsSingleFileFormatsAndOriginalName() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; source format selection unverified");
#else
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        const int gzip = catalog->formatIndex("gzip");
        const int zip = catalog->formatIndex("zip");
        QVERIFY(gzip >= 0 && zip >= 0);
        CCompressDialog state;
        CObjectVector<NWildcard::CCensorPath> censor;
        censor.AddNew().Path = source_.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(state, catalog->nativeCodecs(), censor),
                 S_OK);
        QVERIFY(!state.Info.KeepName);
        QCOMPARE(QString::fromWCharArray(state.OriginalFileName.Ptr())
                     .normalized(QString::NormalizationForm_C),
                 QFileInfo(source_).fileName().normalized(QString::NormalizationForm_C));
        QVERIFY(state.ArcIndices.FindInSorted(unsigned(gzip)) >= 0);
        QVERIFY(state.ArcIndices.FindInSorted(unsigned(zip)) >= 0);
        censor.AddNew().Path = source_.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(state, catalog->nativeCodecs(), censor),
                 S_OK);
        QVERIFY(state.Info.KeepName);
        QCOMPARE(state.ArcIndices.FindInSorted(unsigned(gzip)), -1);
        censor.Clear();
        censor.AddNew().Path = dir_.path().toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(state, catalog->nativeCodecs(), censor),
                 S_OK);
        QVERIFY(state.Info.KeepName);
        QCOMPARE(state.ArcIndices.FindInSorted(unsigned(gzip)), -1);
        CCodecs missing;
        QCOMPARE(NCompressDialog::PrepareFormatSelection(state, missing, censor), E_FAIL);
        QVERIFY(state.ArcIndices.IsEmpty());
#endif
    }
    void sourceSelectionSetsKeepNameBeforeFormatSwitch_data() {
        QTest::addColumn<bool>("relative");
        QTest::newRow("absolute") << false;
        QTest::newRow("relative") << true;
    }

    void sourceSelectionSetsKeepNameBeforeFormatSwitch() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; source-picker naming consumer unverified");
#else
        QFETCH(bool, relative);
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        const QString other = dir_.filePath(QStringLiteral("второй 空 с пробелом.txt"));
        QFile file(other);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("second"), qint64(6));
        file.close();
        CreateArchiveDialog dialog(dir_.path(), {source_, other}, nullptr, catalog);
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        QCOMPARE(dialog.sources().size(), 2);
        name->setText("report.with.dot");
        format->setCurrentText("zip");
        QCOMPARE(name->text(), QStringLiteral("report.with.dot.zip"));
        format->setCurrentText("7z");
        name->setText("report.with.dot.7z ");
        format->setCurrentText("zip");
        QCOMPARE(name->text(), QStringLiteral("report.with.dot.zip"));
        CreateArchiveDialog single(dir_.path(), {source_}, nullptr, catalog);
        QCOMPARE(single.sources(), QStringList{source_});
        single.findChild<QLineEdit *>("archiveName")->setText("report.with.dot");
        single.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        QCOMPARE(single.findChild<QLineEdit *>("archiveName")->text(),
                 QStringLiteral("report.with.zip"));
        const QString subfolder =
            relative ? QStringLiteral("относительная папка") : QStringLiteral("другая папка");
        const QString destination = dir_.filePath(subfolder);
        const QString entered =
            relative ? subfolder + "/custom.zip" : QDir(destination).filePath("custom.zip");
        single.findChild<QLineEdit *>("archiveName")->setText(QDir::toNativeSeparators(entered));
        single.findChild<QComboBox *>("archiveFormat")->setCurrentText("gzip");
        const QString output = QDir(destination).filePath(QFileInfo(source_).fileName() + ".gz");
        submit(single);
        QCOMPARE(single.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(single.target()), output);
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, single.target(), single.sources(), {},
                                               false, single.compressionOptions()),
                 S_OK);
        QProcess reader;
        reader.start("7z", {"x", "-tgzip", "-so", "--", output});
        QVERIFY(reader.waitForFinished(30000));
        QCOMPARE(reader.exitCode(), 0);
        QCOMPARE(reader.readAllStandardOutput(), QByteArray("archive path regression"));
        dialog.reject();
        QVERIFY(dialog.target().isEmpty());
#endif
    }
    void defaultArchiveNameComesFromNativeSourceName() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native default archive name unverified");
#else
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        CreateArchiveDialog dialog(dir_.path(), {source_}, nullptr, catalog);
        const QString name = dialog.findChild<QLineEdit *>("archiveName")
                                 ->text()
                                 .normalized(QString::NormalizationForm_C);
        QCOMPARE(name, QStringLiteral("источник с пробелом.7z"));
        dialog.reject();
        QVERIFY(dialog.target().isEmpty());
#endif
    }
    void sourceFormKeepsFormatEditableForExistingUnknownSuffix() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; Add metadata consumer unverified");
#else
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        const QString target = dir_.filePath(QStringLiteral("архив 空.unknown"));
        QFile archive(target);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        QCOMPARE(archive.write("untouched existing bytes"), qint64(24));
        archive.close();
        CCompressDialog source;
        source.Info.FormatIndex = catalog->formatIndex("zip");
        source.Info.ArcPath = target.toStdWString().c_str();
        CObjectVector<NWildcard::CCensorPath> paths;
        paths.AddNew().Path = source_.toStdWString().c_str();
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog->nativeCodecs(), paths),
                 S_OK);
        CreateArchiveDialog dialog(dir_.path(), {source_}, nullptr, catalog, &source);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        QVERIFY(format);
        QCOMPARE(format->currentText(), QStringLiteral("zip"));
        QVERIFY(format->isEnabled());
        QVERIFY(dialog.findChild<QComboBox *>("archivePath")->isEnabled());
        QVERIFY(dialog.findChild<QPushButton *>("archiveBrowse")->isEnabled());
        QVERIFY(dialog.findChild<QComboBox *>("archiveVolumeSize")->isEnabled());
        QVERIFY(dialog.findChild<QLineEdit *>("archivePassword"));
        dialog.findChild<QLineEdit *>("archiveName")->setText(target);
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.target(), target);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), QByteArray("untouched existing bytes"));
#endif
    }
    void sourceCanReturnBeforeRetryingTheDialog_data() {
        QTest::addColumn<bool>("unreadable");
        QTest::newRow("missing") << false;
#ifdef Q_OS_UNIX
        QTest::newRow("unreadable") << true;
#endif
    }
    void sourceCanReturnBeforeRetryingTheDialog() {
        QFETCH(bool, unreadable);
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable");
#else
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("selected file.txt");
        const QString parked = fixture.filePath("parked.txt");
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("payload"), 7);
        file.close();
        const auto permissions = QFile::permissions(input);
        const auto restore = qScopeGuard([&] {
            if (unreadable)
                QFile::setPermissions(input, permissions);
        });
        if (unreadable) {
            QVERIFY(QFile::setPermissions(input, QFile::WriteOwner));
            if (QFileInfo(input).isReadable())
                QSKIP("The current user can still read the input");
        }
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
        const QString target = fixture.filePath("retry.zip");
        CCompressDialog source;
        source.Info.FormatIndex = catalog->formatIndex("zip");
        source.Info.ArcPath = target.toStdWString().c_str();
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(source_.toStdWString().c_str());
        censor.AddPreItem_NoWildcard(input.toStdWString().c_str());
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, catalog->nativeCodecs(),
                                                         censor.CensorPaths),
                 S_OK);
        bool responded = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            responder.stop();
            const auto reject = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *name = dialog->findChild<QLineEdit *>("archiveName");
            name->setText(target);
            auto *volume = dialog->findChild<QComboBox *>("archiveVolumeSize");
            QVERIFY(volume);
            volume->setEditText("64K");
            if (!unreadable)
                QVERIFY(QFile::rename(input, parked));
            QStringList errors;
            int confirmations = 0;
            QTimer dismiss;
            connect(&dismiss, &QTimer::timeout, dialog, [&] {
                if (auto *message =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    if (auto *yes = message->button(QMessageBox::Yes)) {
                        ++confirmations;
                        yes->click();
                    } else {
                        errors.append(message->text());
                        message->accept();
                    }
                }
            });
            dismiss.start(1);
            submit(*dialog);
            QVERIFY(dialog->isVisible());
            QCOMPARE(confirmations, 0);
            QCOMPARE(errors.size(), 1);
            QVERIFY(errors.first().contains("selected file.txt"));
            QCOMPARE(name->text(), target);
            QCOMPARE(volume->currentText(), QString("64K"));
            if (unreadable)
                QVERIFY(QFile::setPermissions(input, permissions));
            else
                QVERIFY(QFile::rename(parked, input));
            submit(*dialog);
            QCOMPARE(dialog->result(), int(QDialog::Accepted));
            QCOMPARE(confirmations, 1);
            responded = true;
        });
        responder.start(5);
        QCOMPARE(CreateArchiveDialog::PresentSource(source, censor.CensorPaths, catalog, nullptr),
                 S_OK);
        QVERIFY(responded);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(source.Info.ArcPath.Ptr())),
                 target);
#endif
    }

    void unavailableMetadataDisablesSubmission() {
        const auto catalog =
            std::make_shared<ArchiveCodecCatalog>(dir_.filePath("missing module"), 1);
        QVERIFY(catalog->result() != S_OK);
        CreateArchiveDialog dialog(dir_.path(), {source_}, nullptr, catalog);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        QCOMPARE(format->count(), 0);
        QVERIFY(!format->toolTip().isEmpty());
        QVERIFY(!dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
        dialog.reject();
        QVERIFY(dialog.target().isEmpty());
    }
    void formDefersDestinationCreationToUpdateArchive() {
        CreateArchiveDialog dialog(dir_.path(), {source_});
        const QString folder = dir_.filePath(QStringLiteral("новая 空 папка"));
        const QString output = QDir(folder).filePath("nested.7z");
        QVERIFY(!QFileInfo::exists(folder));
        dialog.findChild<QLineEdit *>("archiveName")->setText(output);
        QString error;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                error = message->text();
                message->accept();
            }
        });
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(dialog.target(), output);
        QVERIFY(!QFileInfo::exists(folder));
        QVERIFY(QFileInfo::exists(source_));
    }
    void fullPathsAndFailure() {
        CCompressDialog path;
        path.StartDirPrefix = dir_.path();
        path.m_ArchivePath = QStringLiteral("  вложенная/архив с точкой.7z  ");
        QString resolved;
        QVERIFY(path.GetFinalPath_Smart(resolved));
        QCOMPARE(QDir::fromNativeSeparators(resolved),
                 dir_.filePath(QStringLiteral("вложенная/архив с точкой.7z")));
        path.DirPrefix = dir_.filePath("other");
        QVERIFY(path.GetFinalPath_Smart(resolved));
        QCOMPARE(QDir::fromNativeSeparators(resolved),
                 dir_.filePath(QStringLiteral("other/вложенная/архив с точкой.7z")));
        path.m_ArchivePath = source_;
        QVERIFY(path.GetFinalPath_Smart(resolved));
        QCOMPARE(QDir::fromNativeSeparators(resolved), source_);
        QVERIFY(path.SetArcPathFields(source_));
        QCOMPARE(path.DirPrefix, dir_.path() + '/');
        QCOMPARE(path.m_ArchivePath, QFileInfo(source_).fileName());
        QString name = "unchanged";
        QVERIFY(!path.SetArcPathFields(QString("../").repeated(256) + "refused.7z", name, false));
        QCOMPARE(name, QString("unchanged"));
        path.m_ArchivePath = QString("bad") + QChar::Null + "name.7z";
        QVERIFY(!path.GetFinalPath_Smart(resolved));
        QVERIFY(resolved.isEmpty());
    }
    void boundedHistoryAndSettings() {
        NCompression::CInfo registry;
        for (int i = 0; i < 25; i++)
            registry.ArcPaths.Add(dir_.filePath(QString("old %1.zip").arg(i)));
        registry.ArcPaths.Insert(0, dir_.filePath("NEW.ZIP"));
        CCompressDialog path;
        path.m_ArchivePath = dir_.filePath("new.zip");
        path.Format = "zip";
        path.OnOK(registry);
        QCOMPARE(registry.ArcPaths.Size(), unsigned(20));
        QCOMPARE(registry.ArcPaths[0], path.m_ArchivePath);
        QCOMPARE(registry.ArcPaths[1], dir_.filePath("old 0.zip"));
        registry.Save(settings_);
        NCompression::CInfo loaded;
        loaded.Load(settings_);
        QCOMPARE(loaded.ArcType, QString("zip"));
        QCOMPARE(loaded.ArcPaths.Size(), unsigned(20));
        QCOMPARE(loaded.ArcPaths[0], path.m_ArchivePath);
        loaded.Load(dir_.filePath("absent.ini"));
        QCOMPARE(loaded.ArcType, QString("7z"));
        QVERIFY(loaded.ArcPaths.IsEmpty());
    }
    void historyKeepsCaseDistinctArchives() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString upper = fixture.filePath("Output.zip");
        const QString lower = fixture.filePath("output.zip");
        QVERIFY(NativeArchiveFixture::WriteFiles(upper, {source_}, "zip"));
        if (QFileInfo::exists(lower))
            QSKIP("The test filesystem folds these archive names");
        QVERIFY(NativeArchiveFixture::WriteFiles(lower, {source_}, "zip"));
        NCompression::CInfo registry;
        registry.ArcType = "zip";
        registry.ArcPaths.Add(upper);
        registry.ArcPaths.Add(fixture.filePath("./output.zip"));
        registry.Save(settings_);

        CreateArchiveDialog dialog(fixture.path(), {source_});
        dialog.findChild<QLineEdit *>("archiveName")->setText("output.zip");
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));

        CreateArchiveDialog reopened(fixture.path(), {source_});
        auto *history = reopened.findChild<QComboBox *>("archivePath");
        QCOMPARE(history->count(), 2);
        QCOMPARE(QDir::fromNativeSeparators(history->itemText(0)), lower);
        QCOMPARE(QDir::fromNativeSeparators(history->itemText(1)), upper);
    }

    void historyRestoresTheArchiveWithItsInferredExtension() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("report.zip");
        {
            CreateArchiveDialog dialog(fixture.path(), {source_});
            dialog.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
            dialog.findChild<QLineEdit *>("archiveName")->setText("report");
            submit(dialog);
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }

        CreateArchiveDialog reopened(dir_.path(), {source_});
        auto *format = reopened.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("7z");
        auto *history = reopened.findChild<QComboBox *>("archivePath");
        history->setCurrentIndex(0);
        QVERIFY(QMetaObject::invokeMethod(history, "activated", Q_ARG(int, 0)));
        QCOMPARE(format->currentText(), QString("zip"));
        submit(reopened);
        QCOMPARE(reopened.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(reopened.target()), archive);
    }

    void typingArchivePathKeepsTheTypedNameAndSelectedFormat_data() {
        QTest::addColumn<QString>("historySuffix");
        QTest::newRow("exact-history-match") << QString();
        QTest::newRow("prefix-of-history") << QString(".old");
    }

    void typingArchivePathKeepsTheTypedNameAndSelectedFormat() {
        QFETCH(QString, historySuffix);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath("previous.zip");
        NCompression::CInfo registry;
        registry.ArcPaths.Add(target + historySuffix);
        registry.Save(settings_);
        CreateArchiveDialog dialog(dir_.path(), {source_});
        dialog.show();
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("7z");
        QCOMPARE(format->currentText(), QString("7z"));
        name->setFocus();
        name->selectAll();
        QTest::keyClicks(name, target);
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), target);
        QCOMPARE(dialog.compressionOptions().format, QString("7z"));
    }

    void historySelectionAndCancel() {
        NCompression::CInfo registry;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("готовый архив.ZIP"));
        registry.ArcPaths.Add(archive);
        registry.Save(settings_);
        CreateArchiveDialog dialog(dir_.path(), {source_});
        auto *combo = dialog.findChild<QComboBox *>("archivePath");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 1);
        QCOMPARE(combo->currentText(), QString("archive.7z"));
        combo->setCurrentIndex(0);
        QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
        QCOMPARE(dialog.findChild<QComboBox *>("archiveFormat")->currentText(), QString("zip"));
        QCOMPARE(combo->currentText(), QStringLiteral("готовый архив.ZIP"));
        QCOMPARE(dialog.findChild<QLineEdit *>("archiveFolder")->text(),
                 QDir::toNativeSeparators(fixture.path() + '/'));
        dialog.reject();
        NCompression::CInfo loaded;
        loaded.Load(settings_);
        QCOMPARE(loaded.ArcType, QString("7z"));
        QCOMPARE(loaded.ArcPaths[0], registry.ArcPaths[0]);

        const QString original = fixture.filePath("original.txt");
        QFile originalFile(original);
        QVERIFY(originalFile.open(QIODevice::WriteOnly));
        QCOMPARE(originalFile.write("original"), qint64(8));
        originalFile.close();
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {original}, "zip"));
        CreateArchiveDialog update(dir_.path(), {source_});
        auto *history = update.findChild<QComboBox *>("archivePath");
        history->setCurrentIndex(0);
        QVERIFY(QMetaObject::invokeMethod(history, "activated", Q_ARG(int, 0)));
        submit(update);
        QCOMPARE(update.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(update.target()), archive);
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, update.target(), update.sources(), {},
                                               false, update.compressionOptions()),
                 S_OK);
        QProcess process;
        process.start("7z", {"x", "-o" + fixture.filePath("out"), "--", archive});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        for (const auto &[input, expected] :
             {std::pair{original, QByteArray("original")},
              std::pair{source_, QByteArray("archive path regression")}}) {
            QFile extracted(fixture.filePath("out/" + QFileInfo(input).fileName()));
            QVERIFY(extracted.open(QIODevice::ReadOnly));
            QCOMPARE(extracted.readAll(), expected);
        }
    }
    void archiveFolderUsesInitialPrefix() {
        CreateArchiveDialog dialog(dir_.path(), {source_});
        QCOMPARE(dialog.findChild<QLineEdit *>("archiveFolder")->text(),
                 QDir::toNativeSeparators(dir_.path()));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), dir_.filePath("archive.7z"));
    }
    void editedDestinationUsesSourcePathResolution() {
        QTemporaryDir destination;
        QVERIFY(destination.isValid());
        const QString folder = destination.filePath(QStringLiteral("папка with spaces 空"));
        CreateArchiveDialog dialog(dir_.path(), {source_});
        dialog.findChild<QLineEdit *>("archiveFolder")->setText(QDir::toNativeSeparators(folder));
        dialog.findChild<QLineEdit *>("archiveName")->setText(QStringLiteral("данные.7z"));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()),
                 QDir(folder).filePath(QStringLiteral("данные.7z")));
        QVERIFY(!QFileInfo::exists(folder));
    }
    void relativeDestinationUsesTheDialogsInitialFolder() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString initial = fixture.filePath("initial");
        QVERIFY(QDir().mkpath(initial));
        const QString destination = fixture.filePath(QStringLiteral("архивы 空"));
        const QString name = QStringLiteral("данные.7z");
        CreateArchiveDialog dialog(initial, {source_});
        dialog.findChild<QLineEdit *>("archiveFolder")
            ->setText(QDir::toNativeSeparators(QStringLiteral("../архивы 空")));
        dialog.findChild<QLineEdit *>("archiveName")->setText(name);
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const QString output = QDir(destination).filePath(name);
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), output);
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources()), S_OK);
        QVERIFY(QFileInfo(output).isFile());
    }
    void occupiedDestinationCanBeCorrectedWithoutLosingTheArchiveName_data() {
        QTest::addColumn<QString>("obstruction");
        QTest::newRow("file") << QString("file");
#ifdef Q_OS_WIN
        QTest::newRow("empty-windows-component") << QString("empty-component");
        QTest::newRow("windows-wildcard") << QString("wildcard");
        QTest::newRow("unavailable-drive") << QString("unavailable-drive");
#endif
#ifndef Q_OS_WIN
        QTest::newRow("dangling-link") << QString("link");
        QTest::newRow("read-only") << QString("permissions");
#endif
#ifdef Q_OS_UNIX
        QTest::newRow("no-directory-search") << QString("search-denied");
#endif
    }
    void occupiedDestinationCanBeCorrectedWithoutLosingTheArchiveName() {
        QFETCH(QString, obstruction);
        const bool danglingLink = obstruction == "link";
        const bool readOnly = obstruction == "permissions";
        const bool searchDenied = obstruction == "search-denied";
        const bool wildcard = obstruction == "wildcard";
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
#ifdef Q_OS_WIN
        QFile occupied(fixture.filePath("occupied"));
#else
        QFile occupied(fixture.filePath("occupied <b>file"));
#endif
        QVERIFY(occupied.open(QIODevice::WriteOnly));
        QCOMPARE(occupied.write("keep"), qint64(4));
        occupied.close();
        const QString missing = fixture.filePath("missing folder");
        QString blocker = obstruction == "empty-component" ? fixture.filePath(" ... ")
                          : wildcard                       ? fixture.filePath("wrong?")
                          : danglingLink                   ? fixture.filePath("old shortcut")
                          : readOnly                       ? fixture.filePath("read only")
                          : searchDenied                   ? fixture.filePath("no access")
                                                           : occupied.fileName();
#ifdef Q_OS_WIN
        if (obstruction == "unavailable-drive") {
            blocker.clear();
            for (QChar letter = u'Z'; letter >= u'D'; letter = QChar(letter.unicode() - 1)) {
                const QString root = QString(letter) + ":/";
                if (!QDir(root).exists()) {
                    blocker = root;
                    break;
                }
            }
            if (blocker.isEmpty())
                QSKIP("No unavailable drive letter");
        }
#endif
        if (danglingLink)
            QVERIFY(QFile::link(missing, blocker));
        const QString name = QStringLiteral("архив с пробелом.7z");
        const QString output =
            readOnly ? blocker + "/writable/new output" : fixture.filePath("new/output");
        QFileDevice::Permissions originalPermissions;
        const auto restore = qScopeGuard([&] {
            if (originalPermissions)
                QFile::setPermissions(blocker, originalPermissions);
        });
        if (readOnly || searchDenied) {
            QVERIFY(QDir().mkpath(blocker + "/writable"));
            originalPermissions = QFile::permissions(blocker);
            QVERIFY(QFile::setPermissions(
                blocker, QFile::ReadOwner | (readOnly ? QFile::ExeOwner : QFile::WriteOwner)));
            const QFileInfo entry(blocker);
            if (readOnly ? entry.isWritable() : (!entry.isWritable() || entry.isExecutable()))
                QSKIP("The test filesystem does not enforce this directory permission");
        }
        CreateArchiveDialog dialog(fixture.path(), {source_});
        auto *folder = dialog.findChild<QLineEdit *>("archiveFolder");
        auto *archiveName = dialog.findChild<QLineEdit *>("archiveName");
        archiveName->setText(name);
        const QString blocked = QDir::toNativeSeparators(QDir(blocker).filePath("child"));
        folder->setText(blocked);
        dialog.show();
        QString diagnostic;
        Qt::TextFormat diagnosticFormat = Qt::AutoText;
        const auto submitAndDismissError = [&] {
            diagnostic.clear();
            QTimer::singleShot(0, &dialog, [&] {
                if (auto *message =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    diagnostic = message->text();
                    diagnosticFormat = message->textFormat();
                    message->accept();
                }
            });
            submit(dialog);
        };
        submitAndDismissError();
        QVERIFY(dialog.isVisible());
        QVERIFY(!diagnostic.isEmpty());
        if (obstruction != "empty-component" && !wildcard)
            QVERIFY(diagnostic.contains(QDir::toNativeSeparators(blocker)));
        QCOMPARE(diagnosticFormat, Qt::PlainText);
        QCOMPARE(dialog.focusWidget(), folder);
        QCOMPARE(folder->text(), blocked);
        QCOMPARE(archiveName->text(), name);
        QCOMPARE(folder->selectedText(), blocked);
        auto *clipboard = QApplication::clipboard();
        const auto clearClipboard = qScopeGuard([clipboard] { clipboard->clear(); });
        clipboard->setText(QDir::toNativeSeparators(output));
        QTest::keySequence(folder, QKeySequence::Paste);
        QCOMPARE(folder->text(), QDir::toNativeSeparators(output));
        const QString blockedName = blocked + QDir::separator() + name;
        archiveName->setText(blockedName);
        submitAndDismissError();
        QVERIFY(dialog.isVisible());
        QVERIFY(!diagnostic.isEmpty());
        if (obstruction != "empty-component" && !wildcard)
            QVERIFY(diagnostic.contains(QDir::toNativeSeparators(blocker)));
        if (wildcard)
            QCOMPARE(archiveName->selectedText(), QString("?"));
        else if (obstruction != "empty-component") {
            auto *pathControl = dialog.findChild<QComboBox *>("archivePath");
            QVERIFY(pathControl);
            QVERIFY(dialog.focusWidget() == pathControl || dialog.focusWidget() == archiveName);
            QCOMPARE(archiveName->selectedText(), blocked + QDir::separator());
        }
        QWidget *nameEditor = dialog.focusWidget();
        QVERIFY(nameEditor);
        if (!wildcard)
            QTest::keyClick(nameEditor, Qt::Key_End);
        QTest::keyClicks(nameEditor, "x");
        QString editedName = blockedName;
        if (wildcard)
            editedName.replace(u'?', u'x');
        else
            editedName += u'x';
        QCOMPARE(archiveName->text(), editedName);
        archiveName->setText("  ");
        submitAndDismissError();
        QVERIFY(dialog.isVisible());
        QVERIFY(!diagnostic.isEmpty());
        QWidget *editor = dialog.focusWidget();
        QVERIFY(editor);
        QTest::keyClicks(editor, "retry.7z");
        QCOMPARE(archiveName->text().trimmed(), QString("retry.7z"));
        QVERIFY(!QFileInfo::exists(output));
        archiveName->setText(name);
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), QDir(output).filePath(name));
        ArchiveBackend backend;
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                               false, dialog.compressionOptions()),
                 S_OK);
        QVERIFY(QFileInfo(dialog.target()).isFile());
        CreateArchiveDialog update(output, {source_});
        update.findChild<QLineEdit *>("archiveName")->setText(name);
        submit(update);
        QCOMPARE(update.result(), int(QDialog::Accepted));
        QCOMPARE(update.target(), dialog.target());
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("keep"));
        if (readOnly)
            QVERIFY(!QFileInfo::exists(blocker + "/child"));
        if (danglingLink) {
            QVERIFY(QFileInfo(blocker).isSymLink());
            QVERIFY(!QFileInfo::exists(missing));
        }
    }
    void occupiedArchiveNameCanBeCorrected_data() {
        QTest::addColumn<QString>("obstruction");
#ifdef Q_OS_UNIX
        QTest::newRow("dangling-link") << QString("link");
        QTest::newRow("write-only-archive") << QString("write-only");
#endif
        QTest::newRow("read-only-archive") << QString("read-only");
        QTest::newRow("existing-folder") << QString("folder");
        QTest::newRow("folder-with-inferred-extension") << QString("inferred-extension");
    }
    void occupiedArchiveNameCanBeCorrected() {
        QFETCH(QString, obstruction);
        const bool dangling = obstruction == "link";
        const bool writeOnly = obstruction == "write-only";
        const bool inferredExtension = obstruction == "inferred-extension";
        const bool folder = obstruction == "folder" || inferredExtension;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString missing = fixture.filePath("missing.7z");
        const QString occupied = fixture.filePath("old.7z");
        ArchiveBackend backend;
        QFileDevice::Permissions originalPermissions;
        QByteArray original;
        const auto restore = qScopeGuard([&] {
            if (originalPermissions)
                QFile::setPermissions(occupied, originalPermissions);
        });
        if (folder)
            QVERIFY(QDir().mkdir(occupied));
        else if (dangling)
            QVERIFY(QFile::link(missing, occupied));
        else {
            QCOMPARE(SourceCreationFixture::Create(backend, occupied, {source_}), S_OK);
            originalPermissions = QFile::permissions(occupied);
            QFile archive(occupied);
            QVERIFY(archive.open(QIODevice::ReadOnly));
            original = archive.readAll();
            archive.close();
            QVERIFY(QFile::setPermissions(
                occupied, writeOnly ? QFile::WriteOwner : QFile::ReadOwner | QFile::ReadUser));
            if (writeOnly && QFileInfo(occupied).isReadable())
                QSKIP("The current user can still read the archive");
        }
        QVERIFY(SourceCreationFixture::Create(backend, occupied, {source_}) != S_OK);

        CreateArchiveDialog dialog(fixture.path(), {source_});
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        const QString enteredName = inferredExtension ? "old" : "old.7z";
        const auto draft = QDir::toNativeSeparators("./" + enteredName);
        name->setText(draft);
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        QVERIFY(level);
        const int store = level->findData(0);
        QVERIFY(store >= 0);
        level->setCurrentIndex(store);
        dialog.show();
        QString diagnostic;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                diagnostic = warning->text();
                warning->accept();
            }
        });
        submit(dialog);
        QVERIFY(dialog.isVisible());
        QVERIFY2(diagnostic.contains(QDir::toNativeSeparators(occupied)), qPrintable(diagnostic));
        if (folder)
            QVERIFY2(diagnostic.contains("Archive path is a folder"), qPrintable(diagnostic));
        QCOMPARE(name->text(), draft);
        QCOMPARE(name->selectedText(), enteredName);
        auto *editor = dialog.focusWidget();
        QVERIFY(editor);
        QTest::keyClicks(editor, "new.7z");
        QCOMPARE(name->text(), QDir::toNativeSeparators("./new.7z"));
        QTest::keyClick(dialog.focusWidget(), Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto options = dialog.compressionOptions();
        QCOMPARE(options.Level, 0);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                               false, options),
                 S_OK);
        QVERIFY(QFileInfo(fixture.filePath("new.7z")).isFile());
        if (folder) {
            QVERIFY(QFileInfo(occupied).isDir());
        } else if (dangling) {
            QVERIFY(QFileInfo(occupied).isSymLink());
            QVERIFY(!QFileInfo::exists(missing));
        } else {
            if (writeOnly)
                QVERIFY(QFile::setPermissions(occupied, originalPermissions));
            QFile archive(occupied);
            QVERIFY(archive.open(QIODevice::ReadOnly));
            QCOMPARE(archive.readAll(), original);
        }
    }

    void directorySpellingInNameCanBeCorrected_data() {
        QTest::addColumn<QString>("entered");
        QTest::newRow("trailing-separator") << QString("new folder/");
        QTest::newRow("dot") << QString("new folder/.");
        QTest::newRow("dot-dot") << QString("new folder/nested/..");
    }
    void directorySpellingInNameCanBeCorrected() {
        QFETCH(QString, entered);
        QTemporaryDir output;
        QVERIFY(output.isValid());
        CreateArchiveDialog dialog(output.path(), {source_});
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("7z");
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        name->setText(QDir::toNativeSeparators(entered));
        format->setCurrentText("zip");
        QCOMPARE(format->currentText(), QString("zip"));
        QCOMPARE(name->text(), QDir::toNativeSeparators(entered));
        dialog.show();
        name->setCursorPosition(0);
        bool errorShown = false;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errorShown = true;
                error->accept();
            }
        });
        submit(dialog);
        QVERIFY(dialog.isVisible());
        QVERIFY(errorShown);
        QCOMPARE(name->text(), QDir::toNativeSeparators(entered));
        QVERIFY(!QFileInfo::exists(output.filePath("new folder")));
        const QString prefix = entered.left(entered.lastIndexOf('/') + 1);
        QCOMPARE(name->selectedText(), entered.mid(prefix.size()));
        QTest::keyClicks(name, "archive.zip");
        QCOMPARE(name->text(), QDir::toNativeSeparators(prefix + "archive.zip"));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()),
                 output.filePath(prefix + "archive.zip"));
    }
    void destinationPickerRetainsNameAndCancelDraft_data() {
        QTest::addColumn<QString>("form");
        QTest::newRow("filename") << QString("filename");
        QTest::newRow("folder-occupies-archive-name") << QString("occupied-name");
        QTest::newRow("absolute-path") << QString("absolute");
        QTest::newRow("relative-path") << QString("relative");
        QTest::newRow("relative-zip-path") << QString("relative-zip");
        QTest::newRow("relative-destination") << QString("relative-destination");
        QTest::newRow("missing-folder") << QString("missing");
        QTest::newRow("invalid-folder") << QString("invalid");
        QTest::newRow("invalid-folder-unicode-space") << QString("invalid-folder-unicode-space");
        QTest::newRow("invalid-name") << QString("invalid-name");
        QTest::newRow("directory-with-separator") << QString("directory-separator");
        QTest::newRow("directory-with-dot") << QString("directory-dot");
    }
    void destinationPickerRetainsNameAndCancelDraft() {
        QFETCH(QString, form);
        const bool directoryDraft = form.startsWith("directory-");
        const bool occupiedName = form == "occupied-name";
        const bool relativeDestination = form == "relative-destination";
        const bool zip = form == "relative-zip" || occupiedName;
        const bool invalidName = form == "invalid-name";
        const bool trailingSpace = form == "invalid-folder-unicode-space";
        const bool invalidFolder = form == "invalid" || trailingSpace;
        const QString selectedFormat = zip ? "zip" : "7z";
        QTemporaryDir destination;
        QTemporaryDir original;
        QVERIFY(destination.isValid());
        QVERIFY(original.isValid());
        const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard(
            [&] { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled); });
        CreateArchiveDialog dialog(relativeDestination ? original.path() : dir_.path(), {source_});
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *folder = dialog.findChild<QLineEdit *>("archiveFolder");
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText(selectedFormat);
        const QString draft = QStringLiteral("имя with spaces 空.") + selectedFormat +
                              (trailingSpace ? QString(QChar(0x00a0)) : QString());
        const QString invalidLeaf = draft.left(3) + QChar::Null + draft.mid(3);
        const QString occupiedPath = destination.filePath(draft);
        if (occupiedName)
            QVERIFY(QDir().mkdir(occupiedPath));
        QVERIFY(QDir(original.path()).mkdir("nested"));
        folder->setText(relativeDestination ? QStringLiteral("nested")
                        : form == "missing" ? original.filePath("nested/not created/deeper")
                        : invalidFolder     ? original.path() + QChar::Null
                                            : original.path());
        const QString entered = directoryDraft ? (form == "directory-dot" ? "nested/." : "nested/")
                                : form == "absolute"
                                    ? QDir::toNativeSeparators(original.filePath(draft))
                                : invalidName ? "nested/" + invalidLeaf
                                : form == "relative" || zip || invalidFolder ? "nested/" + draft
                                                                             : draft;
        name->setText(entered);
        dialog.show();
        auto *browse = dialog.findChild<QPushButton *>("archiveBrowse");
        for (bool accept : {false, true}) {
            const QString previousFolder = folder->text();
            folder->setSelection(4, -3);
            bool observed = false;
            QString error;
            QTimer answer;
            connect(&answer, &QTimer::timeout, &dialog, [&] {
                if (observed) {
                    if (auto *message =
                            qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                        error = message->text();
                        answer.stop();
                        message->accept();
                    }
                    return;
                }
                for (auto *widget : QApplication::topLevelWidgets()) {
                    auto *picker = qobject_cast<QFileDialog *>(widget);
                    if (!picker || !picker->isVisible())
                        continue;
                    if (!accept || !occupiedName)
                        answer.stop();
                    observed = true;
                    const auto close = qScopeGuard([&] {
                        if (picker->isVisible())
                            picker->reject();
                    });
                    QCOMPARE(picker->fileMode(), QFileDialog::Directory);
                    const QString initial = invalidFolder || invalidName ? dir_.path()
                                            : form == "relative" || zip || form == "missing" ||
                                                    directoryDraft || relativeDestination
                                                ? original.filePath("nested")
                                                : original.path();
                    QCOMPARE(picker->directory().canonicalPath(),
                             QFileInfo(initial).canonicalFilePath());
                    if (accept) {
                        picker->setDirectory(destination.path());
                        picker->selectFile(destination.path());
                        QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
                    } else {
                        picker->setDirectory(dir_.path());
                        picker->reject();
                    }
                    return;
                }
            });
            answer.start(1);
            browse->setFocus();
            browse->click();
            answer.stop();
            QVERIFY(observed);
            QCOMPARE(name->text(), accept ? (directoryDraft ? QString()
                                             : invalidName  ? invalidLeaf
                                                            : draft)
                                          : entered);
            QCOMPARE(format->currentText(), selectedFormat);
            if (accept && occupiedName) {
                QVERIFY(error.contains(QDir::toNativeSeparators(occupiedPath)));
                QTRY_COMPARE(dialog.focusWidget(), name->focusProxy() ? name->focusProxy() : name);
                QCOMPARE(name->selectedText(), draft);
            } else {
                QVERIFY(error.isEmpty());
            }
            if (accept)
                QCOMPARE(QFileInfo(folder->text()).canonicalFilePath(),
                         QFileInfo(destination.path()).canonicalFilePath());
            else {
                QCOMPARE(folder->text(), previousFolder);
                QTRY_COMPARE(dialog.focusWidget(), static_cast<QWidget *>(folder));
                QCOMPARE(folder->selectedText(), previousFolder.mid(1, 3));
                QCOMPARE(folder->cursorPosition(), 1);
            }
        }
        if (invalidName) {
            QTimer::singleShot(0, &dialog, [] {
                if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    message->accept();
            });
            submit(dialog);
            QCOMPARE(dialog.result(), 0);
            QCOMPARE(name->text(), invalidLeaf);
            name->setText(draft);
        }
        const QString expectedName = directoryDraft ? QStringLiteral("archive.7z")
                                     : occupiedName ? QStringLiteral("corrected.zip")
                                                    : draft;
        if (directoryDraft) {
            QTRY_COMPARE(dialog.focusWidget(), name->focusProxy() ? name->focusProxy() : name);
            QTest::keyClicks(dialog.focusWidget(), expectedName);
            QCOMPARE(name->text(), expectedName);
        } else if (occupiedName) {
            QTest::keyClicks(dialog.focusWidget(), expectedName);
            QCOMPARE(name->text(), expectedName);
        }
        if (invalidName) {
            submit(dialog);
        } else {
            if (!directoryDraft && !occupiedName)
                QTRY_COMPARE(dialog.focusWidget(), folder);
            QTest::keyClick(dialog.focusWidget(), Qt::Key_Return);
        }
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), destination.filePath(expectedName));
        QCOMPARE(dialog.compressionOptions().format, selectedFormat);
        if (zip) {
            ArchiveBackend backend;
            QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                                   false, dialog.compressionOptions()),
                     S_OK);
            QFile archive(dialog.target());
            QVERIFY(archive.open(QIODevice::ReadOnly));
            QCOMPARE(archive.read(4), QByteArray("PK\x03\x04", 4));
            QVERIFY(!QFileInfo::exists(original.filePath("nested/" + draft)));
            if (occupiedName)
                QVERIFY(QFileInfo(occupiedPath).isDir());
        }
    }
    void absoluteArchiveNameCanBypassAnInvalidFolderDraft() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        CreateArchiveDialog dialog(dir_.path(), {source_});
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *folder = dialog.findChild<QLineEdit *>("archiveFolder");
        const QString invalidFolder = fixture.path() + QChar::Null + "/invalid";
        folder->setText(invalidFolder);
        name->setText("report.7z");
        dialog.show();
        bool rejected = false;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                rejected = true;
                warning->accept();
            }
        });
        submit(dialog);
        QVERIFY(rejected && dialog.isVisible());
        QCOMPARE(folder->selectedText(), invalidFolder);

        const QString target = fixture.filePath("report.7z");
        name->setText(QDir::toNativeSeparators(target));
        QTimer::singleShot(0, &dialog, [] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                warning->accept();
        });
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), target);
    }

    void emptyDestinationKeepsInitialPrefixForBrowseAndAccept() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QDir cwd(fixture.path());
        QVERIFY(cwd.mkpath("initial/deeper"));
        const QString initial = cwd.filePath("initial/deeper");
        int parents = 1;
        for (QDir ancestor = cwd; !ancestor.isRoot(); ++parents)
            QVERIFY(ancestor.cdUp());
        // This path is valid from the initial folder, but escapes the root
        // when the picker accidentally resolves it from the process directory.
        const QString draft = QString("../").repeated(parents) + QStringLiteral("архив 空.7z");
        const QString previous = QDir::currentPath();
        const bool nativeDisabled = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
        const auto restore = qScopeGuard([&] {
            QDir::setCurrent(previous);
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, nativeDisabled);
        });
        QVERIFY(QDir::setCurrent(cwd.path()));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        CreateArchiveDialog dialog(initial, {source_});
        dialog.findChild<QLineEdit *>("archiveFolder")->clear();
        dialog.findChild<QLineEdit *>("archiveName")->setText(draft);
        bool pickerOpened = false;
        QString openedAt;
        QString error;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &dialog, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                error = box->text();
                box->accept();
            } else if (auto *picker =
                           qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                pickerOpened = true;
                openedAt = picker->directory().canonicalPath();
                picker->reject();
            }
        });
        answer.start(1);
        dialog.findChild<QPushButton *>("archiveBrowse")->click();
        answer.stop();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(pickerOpened);
        const QString resolved = QDir::cleanPath(initial + '/' + draft);
        QCOMPARE(openedAt, QFileInfo(QFileInfo(resolved).absolutePath()).canonicalFilePath());
        QCOMPARE(dialog.findChild<QLineEdit *>("archiveName")->text(), draft);
        QVERIFY(dialog.findChild<QLineEdit *>("archiveFolder")->text().isEmpty());
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()), resolved);
        QVERIFY(!QFileInfo::exists(dialog.target()));
    }
    void realArchiveAndValidation() {
        CreateArchiveDialog dialog(dir_.path(), {source_});
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        name->setText(QStringLiteral("готовый архив.7z"));
        submit(dialog);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(QDir::fromNativeSeparators(dialog.target()),
                 dir_.filePath(QStringLiteral("готовый архив.7z")));
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QCOMPARE(SourceCreationFixture::Create(backend, dialog.target(), dialog.sources(), {},
                                               false, dialog.compressionOptions()),
                 S_OK);
        QProcess process;
        process.start("7z", {"t", "--", dialog.target()});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const QString output = dir_.filePath("extracted");
        process.start("7z", {"x", "-o" + output, "--", dialog.target()});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        QFile extracted(QDir(output).filePath(QFileInfo(source_).fileName()));
        QVERIFY(extracted.open(QIODevice::ReadOnly));
        QCOMPARE(extracted.readAll(), QByteArray("archive path regression"));
        for (const char *field : {"archiveName", "archiveFolder"}) {
            CreateArchiveDialog invalid(dir_.path(), {source_});
            invalid.findChild<QLineEdit *>(field)->setText(QStringLiteral("bad") + QChar::Null +
                                                           "name.7z");
            QTimer::singleShot(0, &invalid, [] {
                if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    message->accept();
            });
            submit(invalid);
            QCOMPARE(invalid.result(), 0);
        }
        NCompression::CInfo registry;
        registry.Load(settings_);
        QCOMPARE(registry.ArcPaths.Size(), unsigned(1));
        QCOMPARE(registry.ArcPaths[0], dialog.target());
    }
    void geometry() {
        std::shared_ptr<const ArchiveCodecCatalog> catalog;
#ifdef SEVENZIP_CODEC_MODULE
        catalog =
            std::make_shared<ArchiveCodecCatalog>(QString::fromUtf8(SEVENZIP_CODEC_MODULE), 0);
        QCOMPARE(catalog->result(), S_OK);
#endif
        for (const QSize size : {QSize(760, 720), QSize(640, 480)}) {
            CreateArchiveDialog dialog(dir_.path(), {}, nullptr, catalog);
            dialog.resize(size);
            dialog.show();
            QApplication::processEvents();
            auto *combo = dialog.findChild<QComboBox *>("archivePath");
            auto *format = dialog.findChild<QComboBox *>("archiveFormat");
            dialog.findChild<QScrollArea *>()->ensureWidgetVisible(combo);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*combo, dialog));
            QVERIFY(UiInspection::separate(*combo, *format, dialog));
            QVERIFY(
                UiInspection::writeReport(dialog, QString("archive-path-%1").arg(size.width())));
        }
    }
};
QTEST_MAIN(CompressionPathTests)
#include "compression_path_tests.moc"
