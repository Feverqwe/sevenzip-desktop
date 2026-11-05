#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "dialogs/compress/createarchivedialog.h"
#include "gui/UpdateGUI.h"
#include "localization/LangUtils.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "settings/RegistryUtils.h"
#include "support/archive_sections.h"
#include "support/secret_visibility.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QProxyStyle>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

namespace {
class SizeHintFieldsStyle final : public QProxyStyle {
  public:
    SizeHintFieldsStyle() : QProxyStyle("Fusion") {}
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *data) const override {
        if (hint == SH_FormLayoutFieldGrowthPolicy)
            return QFormLayout::FieldsStayAtSizeHint;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
};

class OverlayScrollStyle final : public QProxyStyle {
  public:
    OverlayScrollStyle() : QProxyStyle("Fusion") {}
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *data) const override {
        if (hint == SH_ScrollBar_Transient)
            return true;
        return QProxyStyle::styleHint(hint, option, widget, data);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option,
                    const QWidget *widget) const override {
        if (metric == PM_ScrollView_ScrollBarOverlap)
            return pixelMetric(PM_ScrollBarExtent, option, widget);
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
};
} // namespace

class SourceCompressDialogTests final : public QObject {
    Q_OBJECT
    std::shared_ptr<const ArchiveCodecCatalog> _catalog;
    QString _module;
  private slots:
    void revealPreservesEditingPositionAndConfirmationDraft() {
        QTemporaryDir fixture;
        CreateArchiveDialog dialog(fixture.path());
        dialog.findChild<QCheckBox *>("archivePasswordEnabled")->setChecked(true);
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *confirmation = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        auto *show = dialog.findChild<QCheckBox *>("archiveShowPassword");
        confirmation->setText(QStringLiteral("  отдельный черновик 密碼  "));
        SecretVisibility::verify(dialog, *password, *show);
        QVERIFY(!QTest::currentTestFailed());
        QCOMPARE(confirmation->text(), QStringLiteral("  отдельный черновик 密碼  "));
        const QString passwordDraft = password->text();
        SecretVisibility::verify(dialog, *confirmation, *show);
        QCOMPARE(password->text(), passwordDraft);
    }
    void collapsedSectionsKeepEffectiveOptionsAndPasswordDraft_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void collapsedSectionsKeepEffectiveOptionsAndPasswordDraft() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        CreateArchiveDialog dialog(fixture.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *compression = dialog.findChild<QToolButton *>("archiveCompressionToggle");
        auto *advanced = dialog.findChild<QToolButton *>("archiveCommandLineToggle");
        auto *files = dialog.findChild<QtDisclosure *>("archiveFileOptionsSection");
        auto *volumes = dialog.findChild<QtDisclosure *>("archiveVolumesSection");
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        auto *enabled = dialog.findChild<QCheckBox *>("archivePasswordEnabled");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *repeat = dialog.findChild<QLineEdit *>("archivePasswordConfirmation");
        QVERIFY(compression && advanced && files && volumes);
        QVERIFY(!compression->isChecked());
        QVERIFY(!dialog.findChild<QComboBox *>("compressionMethod")->isVisible());
        QVERIFY(!files->isExpanded());
        QVERIFY(!volumes->isExpanded());
        QVERIFY(!advanced->isChecked());
        QVERIFY(!parameters->isVisible());
        QVERIFY(!enabled->isChecked());
        QApplication::processEvents();
        auto *summary = dialog.findChild<QWidget *>("archiveCommandLineSection")
                            ->findChild<QLabel *>("disclosureSummary");
        QVERIFY(UiInspection::writeReport(dialog, "redesign-add-collapsed-" + language));
        QVERIFY(!summary->isVisible());
        advanced->click();
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-add-expanded-" + language));
        parameters->clear();
        const QString initialSummary = summary->text();
        parameters->setText("tm=off");
        QVERIFY(!summary->isVisible());
        advanced->click();
        QVERIFY(!parameters->isVisible());
        QCOMPARE(QString::fromWCharArray(dialog.compressionOptions().Options.Ptr()),
                 QString("tm=off"));
        QVERIFY(summary->text() != initialSummary);
        QVERIFY(summary->isVisible());
        volumes->setExpanded(true);
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        volume->setEditText("64M 128M");
        volumes->setExpanded(false);
        files->setExpanded(true);
        auto *mode = dialog.findChild<QComboBox *>("archiveUpdateMode");
        mode->setCurrentIndex(mode->findData(int(NCompressDialog::NUpdateMode::kUpdate)));
        files->setExpanded(false);
        QCOMPARE(dialog.compressionOptions().UpdateMode, NCompressDialog::NUpdateMode::kUpdate);
        QCOMPARE(volume->currentText(), QString("64M 128M"));
        compression->click();
        QVERIFY(dialog.findChild<QComboBox *>("compressionMethod")->isVisible());
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        method->setCurrentIndex(method->count() - 1);
        const QString chosenMethod = method->currentText();
        compression->click();
        QVERIFY(!dialog.findChild<QComboBox *>("compressionMethod")->isVisible());
        QVERIFY(dialog.findChild<QtDisclosure *>("archiveCompressionSection")
                    ->findChild<QLabel *>("disclosureSummary")
                    ->isVisible());
        compression->click();
        QCOMPARE(method->currentText(), chosenMethod);
        QCOMPARE(QString::fromWCharArray(dialog.compressionOptions().Options.Ptr()),
                 QString("tm=off"));
        password->setText(QStringLiteral("черновик 密碼"));
        repeat->setText(password->text());
        QVERIFY(enabled->isChecked());
        QCOMPARE(dialog.password(), password->text());
        enabled->click();
        QVERIFY(dialog.password().isEmpty());
        QVERIFY(!password->isVisible());
        enabled->click();
        QCOMPARE(dialog.password(), QStringLiteral("черновик 密碼"));
        QCOMPARE(repeat->text(), password->text());
        QVERIFY(!summary->text().contains(password->text()));
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-add-password-" + language));
        dialog.reject();
    }

    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official native module unavailable");
#else
        _module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        _catalog = std::make_shared<ArchiveCodecCatalog>(_module, 0);
        QCOMPARE(_catalog->result(), S_OK);
#endif
    }
    void parameterDraftSurvivesProfilesAndRejectsNul_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(760, 720);
        QTest::newRow("compact") << QSize(640, 480);
    }
    void parameterDraftSurvivesProfilesAndRejectsNul() {
        QFETCH(QSize, size);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString invalid = QString("tm=off") + QChar::Null + " mt=1";
        {
            CreateArchiveDialog dialog(fixture.path(), {}, nullptr, _catalog);
            auto *format = dialog.findChild<QComboBox *>("archiveFormat");
            auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
            format->setCurrentIndex(format->findText("7z"));
            parameters->setText(invalid);
            format->setCurrentIndex(format->findText("zip"));
            format->setCurrentIndex(format->findText("7z"));
            QCOMPARE(parameters->text(), invalid);
            format->setCurrentIndex(format->findText("zip"));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        const auto savedBytes = [&] {
            QFile file(settings);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const auto original = savedBytes();
        QVERIFY(!original.isEmpty());
        CreateArchiveDialog dialog(fixture.path(), {}, nullptr, _catalog);
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentIndex(format->findText("7z"));
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        QCOMPARE(parameters->text(), invalid);
        auto *section = dialog.findChild<QtDisclosure *>("archiveCommandLineSection");
        section->setExpanded(false);
        dialog.resize(size);
        dialog.show();
        bool reported = false;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                reported = message->icon() == QMessageBox::Critical;
                message->accept();
            }
        });
        auto *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        ok->click();
        QVERIFY(reported);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(savedBytes(), original);
        QVERIFY(dialog.isVisible());
        QVERIFY(section->isExpanded());
        QCOMPARE(parameters->text(), invalid);
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*parameters, dialog));
        parameters->setText(" -mtm=off -mmt=1 ");
        ok->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto source = dialog.sourceInfo();
        const auto properties = NCompressDialog::GetCompressionProperties(source, true);
        bool timestampOff = false, singleThread = false;
        FOR_VECTOR(i, properties) {
            const auto &property = properties[i];
            timestampOff |= property.Name == L"tm" && property.Value == L"off";
            singleThread |= property.Name == L"mt" && property.Value == L"1";
        }
        QVERIFY(timestampOff && singleThread);
    }

    void acceptedSourceOptionsSurviveLaterWidgetChanges_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<UInt32>("sourceLevel");
        QTest::addColumn<UInt32>("effectiveLevel");
        QTest::newRow("7z") << QString("7z") << UInt32(5) << UInt32(5);
        QTest::newRow("tar-auto-level") << QString("tar") << UInt32(-1) << UInt32(0);
    }
    void acceptedSourceOptionsSurviveLaterWidgetChanges() {
        QFETCH(QString, format);
        QFETCH(UInt32, sourceLevel);
        QFETCH(UInt32, effectiveLevel);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath(QStringLiteral("данные с пробелом.txt"));
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        CCompressDialog source;
        source.Info.FormatIndex = _catalog->formatIndex(format);
        const QString target = fixture.filePath("confirmed." + format);
        source.Info.ArcPath = target.toStdWString().c_str();
        source.Info.Password = "confirmed password";
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(UString(input.toStdWString().c_str()));
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, _catalog->nativeCodecs(),
                                                         censor.CensorPaths),
                 S_OK);
        bool changedAfterConfirmation = false;
        QString publishedPassword;
        bool publishedHeaders = false;
        CompressionOptions publishedOptions;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto dismiss = qScopeGuard([dialog] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *level = dialog->findChild<QComboBox *>("compressionLevel");
            auto *headers = dialog->findChild<QCheckBox *>("archiveEncryptHeaders");
            auto *password = dialog->findChild<QLineEdit *>("archivePassword");
            auto *parameters = dialog->findChild<QLineEdit *>("archiveParameters");
            parameters->setText("tm=off");
            if (format == "7z")
                level->setCurrentIndex(level->findData(5u));
            headers->setChecked(true);
            connect(dialog, &QDialog::accepted, dialog,
                    [&, dialog, level, headers, password, parameters] {
                        changedAfterConfirmation = true;
                        password->setText("later draft");
                        parameters->setText("tm=on");
                        headers->setChecked(false);
                        level->setCurrentIndex(level->findData(0u));
                        publishedPassword = dialog->password();
                        publishedHeaders = dialog->encryptHeaders();
                        publishedOptions = dialog->compressionOptions();
                    });
            auto *ok = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
            QVERIFY(ok->isEnabled());
            ok->click();
        });
        QCOMPARE(CreateArchiveDialog::PresentSource(source, censor.CensorPaths, _catalog, nullptr),
                 S_OK);
        QVERIFY(changedAfterConfirmation);
        const QString password = format == "7z" ? QString("confirmed password") : QString();
        QCOMPARE(source.Info.Password, password);
        QCOMPARE(publishedPassword, password);
        QCOMPARE(source.Info.Level, sourceLevel);
        QCOMPARE(publishedOptions.Level, effectiveLevel);
        QCOMPARE(QString::fromWCharArray(source.Info.Options.Ptr()), QString("tm=off"));
        QCOMPARE(QString::fromWCharArray(publishedOptions.Options.Ptr()), QString("tm=off"));
        QCOMPARE(source.Info.EncryptHeaders, format == "7z");
        QCOMPARE(publishedHeaders, format == "7z");
        QCOMPARE(QString::fromWCharArray(source.Info.ArcPath.Ptr()), target);
    }

    void fixedSelectionRoundTrip_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("sizeHintFields");
        for (const QString &lang : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(760, 720), QSize(640, 480)})
                for (const bool hints : {false, true})
                    QTest::newRow(qPrintable(lang + QString::number(size.width()) +
                                             (hints ? "-size-hints" : "")))
                        << lang << size << hints;
    }
    void scrollBarKeepsItsOwnSpace_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("overlayStyle");
        for (const QString &language : {QString("-"), QString("ru")})
            for (bool overlay : {false, true})
                QTest::newRow(qPrintable(language + (overlay ? "-overlay" : "-native")))
                    << language << overlay;
    }
    void scrollBarKeepsItsOwnSpace() {
        QFETCH(QString, language);
        QFETCH(bool, overlayStyle);
        std::unique_ptr<QStyle> previousStyle;
        if (overlayStyle) {
            previousStyle.reset(QApplication::style());
            previousStyle->setParent(nullptr);
            QApplication::setStyle(new OverlayScrollStyle);
        }
        const auto restoreStyle = qScopeGuard([&] {
            if (previousStyle)
                QApplication::setStyle(previousStyle.release());
        });
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        CreateArchiveDialog dialog(fixture.path(), {}, nullptr, _catalog);
        dialog.resize(640, 480);
        dialog.show();
        ExpandArchiveSections(dialog);
        QCoreApplication::processEvents();
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        QVERIFY(scroll);
        auto *bar = scroll->verticalScrollBar();
        QVERIFY(bar->maximum() > 0);
        QVERIFY(bar->isVisible());
        const QRect viewport = UiInspection::bounds(*scroll->viewport(), dialog);
        QVERIFY2(!viewport.intersects(UiInspection::bounds(*bar, dialog)),
                 "The Add scroll bar must reserve space outside the form viewport");
        const QRect barBounds = UiInspection::bounds(*bar, dialog);
        QCOMPARE(barBounds.right(), dialog.rect().right());
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QDialogButtonBox *>(), dialog));
        const QString tag = QString("source-add-scroll-%1-%2")
                                .arg(language == "ru" ? "ru" : "en")
                                .arg(overlayStyle ? "overlay" : "native");
        QVERIFY(UiInspection::writeReport(dialog, tag + "-top"));
        bar->setValue(bar->maximum());
        QCoreApplication::processEvents();
        QCOMPARE(UiInspection::bounds(*scroll->viewport(), dialog), viewport);
        QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QLineEdit *>("archiveParameters"),
                                           dialog));
        QVERIFY(UiInspection::writeReport(dialog, tag + "-bottom"));
        dialog.resize(900, 1100);
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QDialogButtonBox *>(), dialog));
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        QVERIFY(UiInspection::writeReport(dialog, tag + "-expanded"));
    }
    void selectionsKeepColumnGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(760, 720), QSize(640, 480)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }
    void selectionsKeepColumnGeometry() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        CreateArchiveDialog dialog(fixture.path(), {}, nullptr, _catalog);
        dialog.resize(size);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        auto *toggle = dialog.findChild<QToolButton *>("archiveCompressionToggle");
        QVERIFY(toggle);
        toggle->setChecked(true);
        int step = 0;
        const auto inspect = [&] {
            QApplication::processEvents();
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QDialogButtonBox *>(), dialog));
            for (const char *name :
                 {"compressionMethod", "compressionDictionary", "compressionWordSize",
                  "compressionSolidBlock", "compressionThreads", "compressionMemoryLimit"}) {
                auto *control = dialog.findChild<QComboBox *>(name);
                if (!control->isVisible())
                    continue;
                // Reveal the whole field, rather than its input-method cursor rectangle.
                const QRect rect(control->mapTo(scroll->widget(), QPoint()), control->size());
                scroll->ensureVisible(rect.center().x(), rect.center().y(), rect.width() / 2 + 16,
                                      rect.height() / 2 + 16);
                QApplication::processEvents();
                QVERIFY2(UiInspection::fullyVisible(*control, dialog), name);
                QStyleOptionComboBox option;
                option.initFrom(control);
                const QRect contents = control->style()->subControlRect(
                    QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, control);
                QVERIFY2(control->fontMetrics().horizontalAdvance(control->currentText()) <=
                             contents.width(),
                         name);
            }
            QVERIFY(UiInspection::writeReport(dialog, QString("source-add-readable-%1-%2-%3")
                                                          .arg(language)
                                                          .arg(size.width())
                                                          .arg(step++)));
        };
        for (const char *name : {"compressionLevel", "compressionMethod", "compressionDictionary",
                                 "compressionWordSize", "compressionSolidBlock",
                                 "compressionThreads", "compressionMemoryLimit"}) {
            auto *combo = dialog.findChild<QComboBox *>(name);
            const int original = combo->currentIndex();
            combo->setCurrentIndex(combo->count() - 1);
            inspect();
            combo->setCurrentIndex(original);
            inspect();
        }
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        format->setCurrentText("zip");
        inspect();
        format->setCurrentText("7z");
        inspect();
    }
    void originalMethodSelection() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        CreateArchiveDialog dialog(fixture.path(), {}, nullptr, _catalog);
        auto *method = dialog.findChild<QComboBox *>("compressionMethod");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *format = dialog.findChild<QComboBox *>("archiveFormat");
        QVERIFY(method && level && format);
        QCOMPARE(method->currentData().toInt(), -1);
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        QVERIFY(dialog.compressionOptions().Method.IsEmpty());
        method->setCurrentText("PPMd");
        QCOMPARE(QString::fromWCharArray(dialog.sourceInfo().Method.Ptr()), QString("PPMd"));
        format->setCurrentText("zip");
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        format->setCurrentText("7z");
        QCOMPARE(method->currentText(), QString("PPMd"));
        level->setCurrentIndex(level->findData(0u));
        QVERIFY(!method->isEnabled());
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        level->setCurrentIndex(level->findData(5u));
        format->setCurrentText("tar");
        QCOMPARE(level->currentData().toUInt(), 0u);
        QCOMPARE(method->currentText(), QString("*  GNU"));
        QVERIFY(dialog.sourceInfo().Method.IsEmpty());
        method->setCurrentText("POSIX");
        QCOMPARE(QString::fromWCharArray(dialog.sourceInfo().Method.Ptr()), QString("POSIX"));
    }
    void fixedSelectionRoundTrip() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QFETCH(bool, sizeHintFields);
        std::unique_ptr<QStyle> previousStyle;
        if (sizeHintFields) {
            // Retain the actual native style; its object name need not be a
            // QStyleFactory key. QApplication deletes only an app-owned style.
            previousStyle.reset(QApplication::style());
            previousStyle->setParent(nullptr);
            QApplication::setStyle(new SizeHintFieldsStyle);
        }
        const auto restoreStyle = qScopeGuard([&] {
            if (previousStyle)
                QApplication::setStyle(previousStyle.release());
        });
        const QString reportTag = QString("source-add-%1-%2%3")
                                      .arg(language == "ru" ? "ru" : "en")
                                      .arg(size.width())
                                      .arg(sizeHintFields ? "-size-hints" : "");
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空 with spaces.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("abc"), qint64(3));
        file.close();
        CCompressDialog source;
        source.Info.FormatIndex = _catalog->formatIndex("7z");
        source.Info.ArcPath = fixture.filePath("chosen").toStdWString().c_str();
        source.Info.Password = "initial";
        source.Info.UpdateMode = NCompressDialog::NUpdateMode::kFresh;
        source.Info.PathMode = NWildcard::k_FullPath;
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(UString(path.toStdWString().c_str()));
        QCOMPARE(NCompressDialog::PrepareFormatSelection(source, _catalog->nativeCodecs(),
                                                         censor.CensorPaths),
                 S_OK);
        CreateArchiveDialog dialog(fixture.path(), {path}, nullptr, _catalog, &source);
        dialog.resize(size);
        dialog.show();
        QCoreApplication::processEvents();
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        QVERIFY(name);
        QCOMPARE(name->selectedText(), name->text());
        QVERIFY(UiInspection::writeReport(dialog, reportTag));
        auto *folder = dialog.findChild<QLineEdit *>("archiveFolder");
        auto *archiveRow = dialog.findChild<QWidget *>("archivePathRow");
        QVERIFY(folder && archiveRow);
        auto *caption = dialog.findChild<QLabel *>("archiveLabel");
        QVERIFY(caption);
        QVERIFY(UiInspection::separate(*caption, *archiveRow, dialog));
        QVERIFY(UiInspection::bounds(*caption, dialog).right() <
                UiInspection::bounds(*archiveRow, dialog).left());
        QVERIFY(UiInspection::fullyVisible(*archiveRow, dialog));
        QVERIFY(UiInspection::fullyVisible(*folder, dialog));
        ExpandArchiveSections(dialog);
        QCoreApplication::processEvents();
        QCOMPARE(dialog.sources(), QStringList{path});
        auto *mode = dialog.findChild<QComboBox *>("archiveUpdateMode");
        auto *pathMode = dialog.findChild<QComboBox *>("archivePathMode");
        QVERIFY(mode && pathMode);
        QCOMPARE(mode->currentData().toInt(), int(NCompressDialog::NUpdateMode::kFresh));
        QCOMPARE(pathMode->currentData().toInt(), int(NWildcard::k_FullPath));
        auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
        scroll->ensureWidgetVisible(mode);
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*mode, dialog));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons && UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(scroll);
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        scroll->ensureWidgetVisible(dialog.findChild<QLineEdit *>("archiveParameters"));
        QCoreApplication::processEvents();
        QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QLineEdit *>("archiveParameters"),
                                           dialog));
        QVERIFY(UiInspection::writeReport(dialog, reportTag + "-bottom"));
        mode->setCurrentIndex(mode->findData(int(NCompressDialog::NUpdateMode::kUpdate)));
        dialog.findChild<QLineEdit *>("archivePassword")->setText("edited");
        dialog.findChild<QLineEdit *>("archivePasswordConfirmation")->setText("edited");
        // Existing output is intentionally allowed: source UpdateArchive owns
        // update/refusal, while this screen owns presentation and confirmation.
        QFile target(fixture.filePath("chosen.7z"));
        QVERIFY(target.open(QIODevice::WriteOnly));
        target.write("sentinel");
        target.close();
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto result = dialog.sourceInfo();
        QCOMPARE(result.UpdateMode, NCompressDialog::NUpdateMode::kUpdate);
        QCOMPARE(result.Password, QString("edited"));
        QCOMPARE(result.FormatIndex, _catalog->formatIndex("7z"));
        QCOMPARE(QString::fromWCharArray(result.ArcPath.Ptr()), fixture.filePath("chosen.7z"));
        QCOMPARE(source.Info.Password, QString("initial"));
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("sentinel"));
    }
    void actualSourceModalOperation_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("broken");
        QTest::addColumn<bool>("deleteSources");
        QTest::addColumn<bool>("unsupportedDelete");
        QTest::newRow("success") << false << false << false << false;
        QTest::newRow("broken-existing") << false << true << false << false;
        QTest::newRow("modal-cancel") << true << false << false << false;
        QTest::newRow("delete-success") << false << false << true << false;
        QTest::newRow("delete-cancel") << true << false << true << false;
        QTest::newRow("delete-incompatible-mode") << false << false << true << true;
    }
    void actualSourceModalOperation() {
        QFETCH(bool, cancel);
        QFETCH(bool, broken);
        QFETCH(bool, deleteSources);
        QFETCH(bool, unsupportedDelete);
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空 with spaces.txt"));
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("abc");
        source.close();
        const QString output = fixture.filePath("actual.7z");
        if (broken) {
            QFile target(output);
            QVERIFY(target.open(QIODevice::WriteOnly));
            target.write("sentinel");
        }
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _catalog->version()), S_OK);
        bool shown = false;
        unsigned errors = 0;
        QtShellArchiveServices service(
            backend, nullptr,
            {.Error = [&](HRESULT, const UString &) { errors++; },
             .Modal =
                 [&](CCompressDialog &dialog, const CObjectVector<NWildcard::CCensorPath> &paths,
                     QWidget *parent) {
                     return CreateArchiveDialog::PresentSource(dialog, paths,
                                                               backend.codecCatalog(), parent);
                 }});
        QTimer dismissal;
        connect(&dismissal, &QTimer::timeout, [&] {
            if (auto *dialog =
                    qobject_cast<CreateArchiveDialog *>(QApplication::activeModalWidget())) {
                if (!shown) {
                    shown = true;
                    if (unsupportedDelete) {
                        auto *mode = dialog->findChild<QComboBox *>("archiveUpdateMode");
                        mode->setCurrentIndex(
                            mode->findData(int(NCompressDialog::NUpdateMode::kFresh)));
                    }
                    auto *remove = dialog->findChild<QCheckBox *>("archiveDeleteAfterCompression");
                    QVERIFY(remove && remove->isEnabled());
                    remove->setChecked(deleteSources);
                    if (cancel)
                        dialog->reject();
                    else
                        dialog->findChild<QDialogButtonBox *>()
                            ->button(QDialogButtonBox::Ok)
                            ->click();
                }
            } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors++;
                box->accept();
            } else if (auto *progress =
                           dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget())) {
                auto *button = progress->findChild<QPushButton *>("IDCANCEL");
                if (button && button->property("languageID").toUInt() == IDS_CLOSE)
                    button->click();
            }
        });
        dismissal.start(5);
        UStringVector paths;
        paths.Add(UString(path.toStdWString().c_str()));
        service.CompressFiles(UString((fixture.path() + '/').toStdWString().c_str()),
                              UString(L"actual.7z"), UString(L"7z"), false, paths, false, true,
                              false);
        QVERIFY(shown);
        if (cancel) {
            QCOMPARE(service.Result(), E_ABORT);
            QVERIFY(!QFileInfo::exists(output));
            QCOMPARE(errors, 0u);
        } else if (broken) {
            QVERIFY(service.Result() != S_OK);
            QCOMPARE(errors, 1u);
            QFile target(output);
            QVERIFY(target.open(QIODevice::ReadOnly));
            QCOMPARE(target.readAll(), QByteArray("sentinel"));
        } else if (unsupportedDelete) {
            QVERIFY(service.Result() != S_OK);
            QCOMPARE(errors, 1u);
            QVERIFY(!QFileInfo::exists(output));
        } else {
            QCOMPARE(service.Result(), S_OK);
            QCOMPARE(errors, 0u);
            QProcess verify;
            verify.start(QStandardPaths::findExecutable("7z"), {"t", "--", output});
            QVERIFY(verify.waitForFinished(10000));
            QCOMPARE(verify.exitCode(), 0);
        }
        if (deleteSources && !cancel && !broken && !unsupportedDelete)
            QVERIFY(!QFileInfo::exists(path));
        else {
            QVERIFY(source.open(QIODevice::ReadOnly));
            QCOMPARE(source.readAll(), QByteArray("abc"));
        }
        QVERIFY(!backend.busy());
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceCompressDialogTests)
#include "source_compress_dialog_tests.moc"
