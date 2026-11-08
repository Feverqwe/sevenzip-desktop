#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/support/extract_dialog_fixture.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/extract/alias.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/secret_visibility.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

class ExtractDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QByteArray oldSettings_;
    QString settings_;
    QComboBox *path(CExtractDialog &dialog) {
        const QString full = dialog.destination();
        dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
        dialog.findChild<QComboBox *>("extractDestination")->setEditText(full);
        return dialog.findChild<QComboBox *>("extractDestination");
    }
    QComboBox *pathMode(CExtractDialog &dialog) {
        return dialog.findChild<QComboBox *>("extractPathMode");
    }
    QComboBox *overwrite(CExtractDialog &dialog) {
        return dialog.findChild<QComboBox *>("extractOverwriteMode");
    }
  private slots:
    void revealPreservesEditingPosition() {
        CExtractDialog dialog(dir_.path(), {});
        dialog.findChild<QCheckBox *>("extractPasswordEnabled")->setChecked(true);
        SecretVisibility::verify(dialog, *dialog.findChild<QLineEdit *>("extractPassword"),
                                 *dialog.findChild<QCheckBox *>("extractShowPassword"));
    }
    void optionalSectionsKeepDraftAndEffectiveOptions_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void optionalSectionsKeepDraftAndEffectiveOptions() {
        QFETCH(QString, language);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        CExtractDialog dialog(dir_.path(), {});
        dialog.SetInitialOptions(NExtract::NPathMode::kFullPaths, NExtract::NOverwriteMode::kRename,
                                 QStringLiteral("пароль 密碼"));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *enabled = dialog.findChild<QCheckBox *>("extractPasswordEnabled");
        auto *password = dialog.findChild<QLineEdit *>("extractPassword");
        auto *advanced = dialog.findChild<QToolButton *>("extractAdvancedToggle");
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        QVERIFY(enabled->isChecked());
        QVERIFY(!advanced->isChecked());
        QVERIFY(!overwrite(dialog)->isVisible());
        auto *summary = dialog.findChild<QWidget *>("extractAdvancedSection")
                            ->findChild<QLabel *>("disclosureSummary");
        QVERIFY(summary && summary->isVisible());
        overwrite(dialog)->setCurrentIndex(
            overwrite(dialog)->findData(NExtract::NOverwriteMode::kAsk));
        QVERIFY(!summary->isVisible());
        overwrite(dialog)->setCurrentIndex(
            overwrite(dialog)->findData(NExtract::NOverwriteMode::kRename));
        QVERIFY(summary->isVisible());
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-password-" + language));
        enabled->click();
        QVERIFY(dialog.password().isEmpty());
        QVERIFY(!password->isVisible());
        enabled->click();
        QCOMPARE(dialog.password(), QStringLiteral("пароль 密碼"));
        advanced->click();
        scroll->ensureWidgetVisible(overwrite(dialog));
        QTRY_VERIFY(UiInspection::fullyVisible(*overwrite(dialog), dialog));
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-expanded-" + language));
        pathMode(dialog)->setCurrentIndex(0);
        advanced->click();
        QVERIFY(!pathMode(dialog)->isVisible());
        QVERIFY(!overwrite(dialog)->isVisible());
        enabled->click();
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-collapsed-" + language));
        advanced->click();
        pathMode(dialog)->setCurrentIndex(1);
        advanced->click();
        SaveRegLang(UString(language == "ru" ? "-" : "ru"), settings_);
        ReloadLang(settings_);
        QtNotifyLanguageChanged();
        QCOMPARE(pathMode(dialog)->currentText(),
                 QString::fromWCharArray(LangString(IDS_EXTRACT_PATHS_NO)).remove('&'));
        QCOMPARE(overwrite(dialog)->currentText(),
                 QString::fromWCharArray(LangString(IDS_EXTRACT_OVERWRITE_RENAME)).remove('&'));
        dialog.accept();
        QCOMPARE(dialog.Password, QString());
        QCOMPARE(dialog.PathMode, NExtract::NPathMode::kNoPaths);
        QCOMPARE(dialog.OverwriteMode, NExtract::NOverwriteMode::kRename);
    }

    void compactFieldsRemainReachable_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void compactFieldsRemainReachable() {
        QFETCH(QString, language);
        auto *hints = QGuiApplication::styleHints();
        const auto previousTabFocus = hints->tabFocusBehavior();
        const auto restoreTabFocus =
            qScopeGuard([&] { hints->setTabFocusBehavior(previousTabFocus); });
        hints->setTabFocusBehavior(Qt::TabFocusAllControls);
        SaveRegLang(UString(language.toStdWString().c_str()), settings_);
        ReloadLang(settings_);
        CExtractDialog dialog(dir_.filePath(QStringLiteral("длинная папка 空")), {});
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QApplication::processEvents();
        auto *advanced = dialog.findChild<QToolButton *>("extractAdvancedToggle");
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(advanced && scroll && buttons);
        dialog.resize(360, 300);
        dialog.findChild<QCheckBox *>("extractPasswordEnabled")->setChecked(true);
        advanced->setChecked(true);
        auto *showPassword = dialog.findChild<QCheckBox *>("extractShowPassword");
        showPassword->setFocus();
        QTest::keyClick(showPassword, Qt::Key_Tab);
        QTRY_VERIFY(advanced->hasFocus());
        QTest::keyClick(advanced, Qt::Key_Tab);
        QTRY_VERIFY(overwrite(dialog)->hasFocus());
        for (const char *name :
             {"extractDestination", "extractPathName", "extractPassword", "extractShowPassword",
              "extractOverwriteMode", "extractPathMode", "extractElimDup"}) {
            auto *control = dialog.findChild<QWidget *>(name);
            QVERIFY(control);
            scroll->ensureWidgetVisible(control);
            QTRY_VERIFY2(UiInspection::fullyVisible(*control, dialog), name);
            QVERIFY(control->height() >= control->minimumSizeHint().height());
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        }
        QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
        const auto *pathCaption = dialog.findChild<QLabel *>("extractPathModeLabel");
        QVERIFY(pathCaption && UiInspection::labelFits(*pathCaption));
        QVERIFY(pathCaption->heightForWidth(pathCaption->width()) <
                2 * pathCaption->fontMetrics().lineSpacing());
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-compact-" + language));
        const QSize compactSize = dialog.size();
        dialog.hide();
        dialog.show();
        QApplication::processEvents();
        QCOMPARE(dialog.size(), compactSize);
        dialog.reject();
    }

    void archiveSourcesAreReadOnlyAndFollowLanguage() {
        for (int count : {1, 2, 5, 21}) {
            QStringList sources;
            for (int i = 0; i < count; ++i)
                sources << dir_.filePath(QStringLiteral("Длинное имя <архив> 空 %1.7z").arg(i));
            CExtractDialog dialog(dir_.filePath("output"), {});
            dialog.SetArchiveSources(sources, count != 5);
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            QApplication::processEvents();
            auto *summary = dialog.findChild<QWidget *>("extractSourceSummary");
            auto *section = dialog.findChild<QWidget *>("extractSourcesSection");
            auto *names = dialog.findChild<QPlainTextEdit *>("extractArchiveSources");
            auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
            QVERIFY(summary && section && names && scroll);
            const QString destination = dialog.destination();
            for (const char *language : {"-", "ru"}) {
                SaveRegLang(UString(language), settings_);
                ReloadLang(settings_);
                QtNotifyLanguageChanged();
                const bool russian = QString(language) == "ru";
                const QString name = count == 1   ? QFileInfo(sources.first()).fileName()
                                     : !russian   ? QString("%1 archives").arg(count)
                                     : count == 2 ? QStringLiteral("2 архива")
                                     : count == 5 ? QStringLiteral("5 архивов")
                                                  : QStringLiteral("21 архив");
                QCOMPARE(summary->accessibleName(), name);
                const QString scope = count == 5 ? (russian ? QStringLiteral("По заданным фильтрам")
                                                            : QString("Filtered contents"))
                                                 : (russian ? QStringLiteral("Всё содержимое")
                                                            : QString("Entire contents"));
                QCOMPARE(summary->accessibleDescription(), scope + '\n' + sources.join('\n'));
                QCOMPARE(summary->toolTip(), sources.join('\n'));
                QCOMPARE(names->toPlainText(), sources.join('\n'));
                QVERIFY(names->isReadOnly());
                QCOMPARE(section->isVisible(), count > 1);
                for (const QSize size : {QSize(640, 420), QSize(360, 300)}) {
                    dialog.resize(size);
                    scroll->ensureWidgetVisible(summary);
                    QTRY_VERIFY(UiInspection::fullyVisible(*summary, dialog));
                    for (auto *label : summary->findChildren<QLabel *>())
                        if (label->isVisible())
                            QVERIFY(UiInspection::labelFits(*label));
                    QVERIFY(UiInspection::fullyVisible(*dialog.findChild<QDialogButtonBox *>(),
                                                       dialog));
                    QTRY_COMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                    QVERIFY(UiInspection::writeReport(dialog, QString("extract-source-%1-%2-%3")
                                                                  .arg(language)
                                                                  .arg(count)
                                                                  .arg(size.width())));
                }
                if (count > 1) {
                    auto *toggle = section->findChild<QToolButton *>();
                    toggle->setChecked(true);
                    QVERIFY(names->isVisible());
                    scroll->ensureWidgetVisible(names);
                    QTRY_VERIFY(UiInspection::fullyVisible(*names, dialog));
                    if (count == 2)
                        QVERIFY(UiInspection::writeReport(dialog, "extract-source-list-" +
                                                                      QString(language)));
                    toggle->setChecked(false);
                }
                QCOMPARE(dialog.destination(), destination);
            }
            dialog.reject();
        }
    }

    void realCallerShowsSourcesWithoutChangingFilters() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath(QStringLiteral("1 архив 空.zip"));
        const QString second = fixture.filePath(QStringLiteral("2 архив δ.zip"));
        NativeArchiveFixture::Item wanted, omitted, other, directory;
        wanted.Path = "wanted.txt";
        wanted.Bytes = "first payload";
        omitted.Path = "omitted.txt";
        omitted.Bytes = "excluded payload";
        other.Path = "other.txt";
        other.Bytes = "second payload";
        directory.Path = "empty";
        directory.Directory = true;
        QVERIFY(NativeArchiveFixture::Write(first, {wanted, omitted, directory}, "zip"));
        QVERIFY(NativeArchiveFixture::Write(second, {other}, "zip"));
        ArchiveBackend backend;
        const QString cancelledOutput = fixture.filePath("cancelled");
        const auto cancelled =
            ExtractDialogFixture::Run(backend, QStringList{first, second}, cancelledOutput,
                                      [](CExtractDialog &dialog) { dialog.reject(); });
        QCOMPARE(cancelled.Result, E_ABORT);
        QVERIFY(!QFileInfo::exists(cancelledOutput));
        for (int mode : {0, 1, 2, 3}) {
            const bool filtered = mode != 0;
            const QStringList archives = filtered ? QStringList{first} : QStringList{first, second};
            const QString output = fixture.filePath(QString("output-%1").arg(mode));
            bool sourceMatches = false;
            const auto result = ExtractDialogFixture::Run(
                backend, archives, output,
                [&](CExtractDialog &dialog) {
                    auto *summary = dialog.findChild<QWidget *>("extractSourceSummary");
                    auto *names = dialog.findChild<QPlainTextEdit *>("extractArchiveSources");
                    sourceMatches = summary && names && summary->isVisible() &&
                                    names->toPlainText() == archives.join('\n') &&
                                    summary->accessibleDescription().startsWith(
                                        filtered ? "Filtered contents\n" : "Entire contents\n");
                    if (sourceMatches)
                        dialog.accept();
                    else
                        dialog.reject();
                },
                [&](NWildcard::CCensorNode &censor, CExtractOptions &options) {
                    options.ExcludeFileItems = mode == 2;
                    options.ExcludeDirItems = mode == 3;
                    if (mode != 1)
                        return;
                    NWildcard::CItem exclusion;
                    exclusion.PathParts.Add(L"omitted.txt");
                    exclusion.ForFile = exclusion.ForDir = true;
                    exclusion.Recursive = exclusion.WildcardMatching = false;
                    censor.AddItem(false, exclusion);
                });
            QVERIFY(sourceMatches);
            QCOMPARE(result.Result, S_OK);
            QVERIFY(result.CallbackOK);
            const auto bytes = [](const QString &path) {
                QFile file(path);
                return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
            };
            QCOMPARE(bytes(QDir(output).filePath("wanted.txt")),
                     mode == 2 ? QByteArray() : wanted.Bytes);
            QCOMPARE(QFileInfo::exists(QDir(output).filePath("omitted.txt")),
                     mode == 0 || mode == 3);
            QCOMPARE(QFileInfo::exists(QDir(output).filePath("other.txt")), !filtered);
            QCOMPARE(QFileInfo(QDir(output).filePath("empty")).isDir(), mode != 3);
        }
    }

    void initTestCase() {
        QVERIFY(dir_.isValid());
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        settings_ = dir_.filePath("extract.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.toUtf8());
    }
    void init() {
        QFile::remove(settings_);
        SaveRegLang(UString("-"), settings_);
        ReloadLang(settings_);
        QFile::remove(settings_);
    }
    void cleanupTestCase() {
        if (oldSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
    }
    void malformedPasswordAndSplitPreferences() {
        {
            QSettings key(settings_, QSettings::IniFormat);
            key.setValue("Extraction/SplitDest", "invalid");
            key.setValue("Extraction/ShowPassword", "invalid");
        }
        {
            CExtractDialog dialog(dir_.filePath("destination 空"), {});
            QVERIFY(dialog.findChild<QCheckBox *>("extractNameEnable")->isChecked());
            QVERIFY(!dialog.findChild<QCheckBox *>("extractShowPassword")->isChecked());
            QCOMPARE(dialog.findChild<QLineEdit *>("extractPassword")->echoMode(),
                     QLineEdit::Password);
            dialog.findChild<QCheckBox *>("extractShowPassword")->setChecked(true);
            dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
            dialog.accept();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
        }
        CExtractDialog restarted(dir_.filePath("destination 空"), {});
        QVERIFY(!restarted.findChild<QCheckBox *>("extractNameEnable")->isChecked());
        QVERIFY(restarted.findChild<QCheckBox *>("extractShowPassword")->isChecked());
        QCOMPARE(restarted.findChild<QLineEdit *>("extractPassword")->echoMode(),
                 QLineEdit::Normal);
    }
    void originalModesAndPersistence() {
        {
            CExtractDialog dialog(dir_.path(), {});
            QCOMPARE(overwrite(dialog)->itemText(1), QStringLiteral("Overwrite without prompt"));
            QCOMPARE(overwrite(dialog)->itemText(2), QStringLiteral("Skip existing files"));
            pathMode(dialog)->setCurrentIndex(1);
            overwrite(dialog)->setCurrentIndex(
                overwrite(dialog)->findData(int(NExtract::NOverwriteMode::kRename)));
            dialog.findChild<QCheckBox *>("extractShowPassword")->setChecked(true);
            dialog.findChild<QLineEdit *>("extractPassword")
                ->setText(QStringLiteral("секрет с пробелом"));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(dialog.PathMode, NExtract::NPathMode::kNoPaths);
            QCOMPARE(dialog.OverwriteMode, NExtract::NOverwriteMode::kRename);
            QCOMPARE(dialog.Password, QStringLiteral("секрет с пробелом"));
            QVERIFY(QDir::fromNativeSeparators(dialog.destination()).endsWith('/'));
        }
        CExtractDialog restarted(dir_.path(), {});
        QCOMPARE(pathMode(restarted)->currentIndex(), 1);
        QCOMPARE(overwrite(restarted)->currentData().toInt(),
                 int(NExtract::NOverwriteMode::kRename));
        QCOMPARE(restarted.findChild<QLineEdit *>("extractPassword")->echoMode(),
                 QLineEdit::Normal);
        QVERIFY(restarted.password().isEmpty());
        QFile saved(settings_);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QVERIFY(!saved.readAll().contains(QStringLiteral("секрет").toUtf8()));
        pathMode(restarted)->setCurrentIndex(0);
        overwrite(restarted)->setCurrentIndex(0);
        restarted.reject();
        CExtractDialog afterCancel(dir_.path(), {});
        QCOMPARE(pathMode(afterCancel)->currentIndex(), 1);
        QCOMPARE(overwrite(afterCancel)->currentData().toInt(),
                 int(NExtract::NOverwriteMode::kRename));
    }
    void cancelKeepsPreviouslyAcceptedExtractionOptions() {
        const QString original = dir_.filePath("previous output");
        const QString abandoned = dir_.filePath("abandoned output");
        {
            CExtractDialog accepted(original, {});
            accepted.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
            accepted.findChild<QComboBox *>("extractDestination")->setEditText(original);
            pathMode(accepted)->setCurrentIndex(
                pathMode(accepted)->findData(int(NExtract::NPathMode::kNoPaths)));
            overwrite(accepted)->setCurrentIndex(
                overwrite(accepted)->findData(int(NExtract::NOverwriteMode::kSkip)));
            accepted.accept();
            QCOMPARE(accepted.result(), int(QDialog::Accepted));
        }
        {
            CExtractDialog canceled(original, {});
            canceled.show();
            canceled.findChild<QComboBox *>("extractDestination")->setEditText(abandoned);
            canceled.findChild<QCheckBox *>("extractNameEnable")->setChecked(true);
            canceled.findChild<QLineEdit *>("extractPathName")->setText("discard this");
            pathMode(canceled)->setCurrentIndex(
                pathMode(canceled)->findData(int(NExtract::NPathMode::kFullPaths)));
            overwrite(canceled)->setCurrentIndex(
                overwrite(canceled)->findData(int(NExtract::NOverwriteMode::kOverwrite)));
            QTest::keyClick(&canceled, Qt::Key_Escape);
            QCOMPARE(canceled.result(), int(QDialog::Rejected));
        }
        CExtractDialog reopened(original, {});
        QCOMPARE(pathMode(reopened)->currentData().toInt(), int(NExtract::NPathMode::kNoPaths));
        QCOMPARE(overwrite(reopened)->currentData().toInt(), int(NExtract::NOverwriteMode::kSkip));
        QVERIFY(!reopened.findChild<QCheckBox *>("extractNameEnable")->isChecked());
        auto *history = reopened.findChild<QComboBox *>("extractDestination");
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(history->itemText(0))), original);
        for (int i = 0; i < history->count(); ++i)
            QVERIFY(!history->itemText(i).contains("abandoned output"));
    }

    void legacyModesUseSafeDialogDefaults() {
        NExtract::CInfo saved;
        saved.PathMode = NExtract::NPathMode::kAbsPaths;
        saved.PathMode_Force = true;
        saved.OverwriteMode = NExtract::NOverwriteMode::kRenameExisting;
        saved.OverwriteMode_Force = true;
        saved.Save(settings_);
        CExtractDialog dialog(dir_.path(), {});
        QVERIFY(pathMode(dialog)->findData(int(saved.PathMode)) < 0);
        QVERIFY(overwrite(dialog)->findData(int(saved.OverwriteMode)) < 0);
        QCOMPARE(pathMode(dialog)->currentData().toInt(), int(NExtract::NPathMode::kFullPaths));
        QCOMPARE(overwrite(dialog)->currentData().toInt(), int(NExtract::NOverwriteMode::kAsk));
        dialog.accept();
        QCOMPARE(dialog.PathMode, NExtract::NPathMode::kFullPaths);
        QCOMPARE(dialog.OverwriteMode, NExtract::NOverwriteMode::kAsk);
    }
    void sourceOptionsRespectForcedAndSavedModes() {
        NExtract::CInfo saved;
        saved.PathMode = NExtract::NPathMode::kNoPaths;
        saved.PathMode_Force = true;
        saved.OverwriteMode = NExtract::NOverwriteMode::kSkip;
        saved.OverwriteMode_Force = true;
        saved.Save(settings_);
        CExtractDialog dialog(dir_.path(), {});
        dialog.SetInitialOptions(NExtract::NPathMode::kFullPaths, NExtract::NOverwriteMode::kAsk,
                                 QStringLiteral("пароль с пробелом"));
        QCOMPARE(pathMode(dialog)->currentIndex(), 1);
        QCOMPARE(overwrite(dialog)->currentIndex(), 2);
        QCOMPARE(dialog.findChild<QLineEdit *>("extractPassword")->text(),
                 QStringLiteral("пароль с пробелом"));
        CExtractDialog forced(dir_.path(), {});
        forced.PathMode_Force = true;
        forced.OverwriteMode_Force = true;
        forced.SetInitialOptions(NExtract::NPathMode::kFullPaths, NExtract::NOverwriteMode::kAsk,
                                 QString());
        QCOMPARE(pathMode(forced)->currentIndex(), 0);
        QCOMPARE(overwrite(forced)->currentIndex(), 0);
        QVERIFY(forced.findChild<QLineEdit *>("extractPassword")->text().isEmpty());
        CExtractDialog current(dir_.path(), {});
        current.PathMode_Force = true;
        current.OverwriteMode_Force = true;
        current.SetInitialOptions(NExtract::NPathMode::kCurPaths, NExtract::NOverwriteMode::kAsk,
                                  QString());
        current.accept();
        QCOMPARE(current.PathMode, NExtract::NPathMode::kCurPaths);
        saved.Load(settings_);
        QCOMPARE(saved.PathMode, NExtract::NPathMode::kFullPaths);
    }
    void originalHistoryRules() {
        QStringList history;
        for (int i = 0; i < 20; i++)
            history.append(QString("/history/%1/").arg(i));
        {
            CExtractDialog dialog("/initial", history);
            QCOMPARE(path(dialog)->count(), 16);
            QCOMPARE(path(dialog)->currentText(), QString("/initial"));
            path(dialog)->setCurrentIndex(5);
            dialog.accept();
        }
        NExtract::CInfo saved;
        saved.Load(settings_);
        QCOMPARE(saved.Paths.Size(), 16u);
        QCOMPARE(saved.Paths[0], history[5]);
        QCOMPARE(saved.Paths[1], history[0]);
        {
            CExtractDialog dialog("/initial", {});
            path(dialog)->setCurrentIndex(2);
            path(dialog)->setEditText(QStringLiteral("  /новая папка/δ space  "));
            dialog.accept();
            QCOMPARE(QDir::fromNativeSeparators(dialog.destination()),
                     QStringLiteral("/новая папка/δ space/"));
        }
        saved.Load(settings_);
        QCOMPARE(saved.Paths.Size(), 16u);
        QCOMPARE(QDir::fromNativeSeparators(saved.Paths[0]),
                 QStringLiteral("/новая папка/δ space/"));
        QVERIFY(saved.Paths[15] != history[15]);
        {
            CExtractDialog dialog("/initial", {" /DATA/ ", "/data/", " /different/ "});
            path(dialog)->setCurrentIndex(0);
            dialog.accept();
        }
        saved.Load(settings_);
        QCOMPARE(saved.Paths.Size(), 2u);
        QCOMPARE(saved.Paths[0], QString("/DATA/"));
        QCOMPARE(saved.Paths[1], QString("/different/"));
    }
    void legacyAndInvalidSettings() {
        {
            QSettings settings(settings_, QSettings::IniFormat);
            settings.setValue("history/extract", QStringList{"/legacy/"});
            settings.setValue("Extraction/ExtractMode", 99u);
            settings.setValue("Extraction/OverwriteMode", "invalid");
            settings.setValue("Toolbar/labels", false);
        }
        CExtractDialog dialog("/initial", {});
        QCOMPARE(path(dialog)->itemText(0), QString("/legacy/"));
        QCOMPARE(pathMode(dialog)->currentIndex(), 0);
        QCOMPARE(overwrite(dialog)->currentIndex(), 0);
        dialog.accept();
        QCOMPARE(dialog.OverwriteMode, NExtract::NOverwriteMode::kAsk);
        QSettings settings(settings_, QSettings::IniFormat);
        QCOMPARE(settings.value("Toolbar/labels").toBool(), false);
    }
    void extractionPreservesInputArchives_data() {
        QTest::addColumn<bool>("batch");
        QTest::newRow("same-archive") << false;
        QTest::newRow("later-archive-in-batch") << true;
    }
    void extractionPreservesInputArchives() {
        QFETCH(bool, batch);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("a.zip");
        const QString target = batch ? fixture.filePath("b.zip") : first;
        const QByteArray payload("extracted payload");
        QVERIFY(
            NativeArchiveFixture::Write(first, {{QFileInfo(target).fileName(), payload}}, "zip"));
        if (batch)
            QVERIFY(NativeArchiveFixture::Write(target, {{"second.txt", "keep"}}, "zip"));
        const QStringList inputs = batch ? QStringList{first, target} : QStringList{first};
        const QByteArray firstBytes = SourceExtractFixture::readFile(first);
        const QByteArray targetBytes = SourceExtractFixture::readFile(target);
        ArchiveBackend backend;
        const auto extract = [&](NExtract::NOverwriteMode::EEnum mode) {
            return ExtractDialogFixture::Run(
                backend, inputs, fixture.path(), [&](CExtractDialog &dialog) {
                    dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(false);
                    path(dialog)->setEditText(fixture.path());
                    overwrite(dialog)->setCurrentIndex(overwrite(dialog)->findData(int(mode)));
                    dialog.accept();
                });
        };
        const auto rejected = extract(NExtract::NOverwriteMode::kOverwrite);
        QVERIFY(rejected.Result != S_OK || !rejected.CallbackOK);
        QCOMPARE(SourceExtractFixture::readFile(first), firstBytes);
        QCOMPARE(SourceExtractFixture::readFile(target), targetBytes);
        const auto skipped = extract(NExtract::NOverwriteMode::kSkip);
        QCOMPARE(skipped.Result, S_OK);
        QVERIFY(skipped.CallbackOK);
        QCOMPARE(SourceExtractFixture::readFile(target), targetBytes);
        const auto renamed = extract(NExtract::NOverwriteMode::kRename);
        QCOMPARE(renamed.Result, S_OK);
        QVERIFY(renamed.CallbackOK);
        QCOMPARE(SourceExtractFixture::readFile(target), targetBytes);
        const QString output = fixture.filePath(QFileInfo(target).completeBaseName() + "_1.zip");
        QCOMPARE(SourceExtractFixture::readFile(output), payload);
    }
    void realExtractionUsesDialogModes_data() {
        QTest::addColumn<int>("pathModeIndex");
        QTest::addColumn<QString>("literalPart");
        QTest::newRow("full-paths") << 0 << QString();
        QTest::newRow("no-paths") << 1 << QString();
#ifdef Q_OS_UNIX
        QTest::newRow("literal-base") << 0 << QStringLiteral("base");
        QTest::newRow("literal-subfolder") << 1 << QStringLiteral("subfolder");
#endif
    }
    void realExtractionUsesDialogModes() {
        QFETCH(int, pathModeIndex);
        QFETCH(QString, literalPart);
        const QString leaf = QStringLiteral("файл с пробелом.txt");
        ArchiveBackend backend;
        const QString archive = dir_.filePath("fixture.zip");
        QFile::remove(archive);
        NativeArchiveFixture::Item item;
        item.Path = "nested/" + leaf;
        item.Bytes = "archive content";
        QVERIFY(NativeArchiveFixture::Write(archive, {item}, "zip"));
        const QString plainOutput =
            dir_.filePath(QStringLiteral("папка извлечения %1 %2")
                              .arg(pathModeIndex)
                              .arg(literalPart.isEmpty() ? QString("plain") : literalPart));
        const QString output = literalPart.isEmpty() ? plainOutput : plainOutput + ' ';
        const QString relative = pathModeIndex == 0 ? "nested/" + leaf : leaf;
        QFile existing(QDir(output).filePath(relative));
        QVERIFY(QDir().mkpath(QFileInfo(existing).absolutePath()));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("existing"), qint64(8));
        existing.close();
        const QString other = QDir(plainOutput).filePath(relative);
        if (!literalPart.isEmpty()) {
            QVERIFY(QDir().mkpath(QFileInfo(other).absolutePath()));
            QVERIFY(SourceExtractFixture::writeFile(other, "other folder"));
        }
        const auto extract = [&](int overwriteIndex) {
            return ExtractDialogFixture::Run(backend, archive, output, [&](CExtractDialog &dialog) {
                const bool split = literalPart == "subfolder";
                path(dialog)->setEditText(split ? QFileInfo(output).absolutePath() : output);
                dialog.findChild<QCheckBox *>("extractNameEnable")->setChecked(split);
                if (split)
                    dialog.findChild<QLineEdit *>("extractPathName")
                        ->setText(QFileInfo(output).fileName());
                pathMode(dialog)->setCurrentIndex(pathModeIndex);
                overwrite(dialog)->setCurrentIndex(overwriteIndex);
                dialog.findChild<QCheckBox *>("extractElimDup")->setChecked(false);
                dialog.accept();
            });
        };
        const auto skipped = extract(2);
        QCOMPARE(skipped.Result, S_OK);
        QVERIFY(skipped.CallbackOK);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("existing"));
        existing.close();
        const auto overwritten = extract(1);
        QCOMPARE(overwritten.Result, S_OK);
        QVERIFY(overwritten.CallbackOK);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("archive content"));
        if (!literalPart.isEmpty())
            QCOMPARE(SourceExtractFixture::readFile(other), QByteArray("other folder"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ExtractDialogTests)
#include "extract_dialog_tests.moc"
