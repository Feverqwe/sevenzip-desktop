#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/support/extract_dialog_fixture.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/extract/alias.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/secret_visibility.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QSettings>
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
        dialog.SetInitialOptions(NExtract::NPathMode::kNoPaths, NExtract::NOverwriteMode::kRename,
                                 QStringLiteral("пароль 密碼"));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *enabled = dialog.findChild<QCheckBox *>("extractPasswordEnabled");
        auto *password = dialog.findChild<QLineEdit *>("extractPassword");
        auto *advanced = dialog.findChild<QToolButton *>("extractAdvancedToggle");
        QVERIFY(enabled->isChecked());
        QVERIFY(!advanced->isChecked());
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-password-" + language));
        enabled->click();
        QVERIFY(dialog.password().isEmpty());
        QVERIFY(!password->isVisible());
        enabled->click();
        QCOMPARE(dialog.password(), QStringLiteral("пароль 密碼"));
        advanced->click();
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, "redesign-extract-expanded-" + language));
        pathMode(dialog)->setCurrentIndex(0);
        advanced->click();
        QVERIFY(!pathMode(dialog)->isVisible());
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
            overwrite(dialog)->setCurrentIndex(4);
            dialog.findChild<QCheckBox *>("extractShowPassword")->setChecked(true);
            dialog.findChild<QLineEdit *>("extractPassword")
                ->setText(QStringLiteral("секрет с пробелом"));
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(dialog.PathMode, NExtract::NPathMode::kNoPaths);
            QCOMPARE(dialog.OverwriteMode, NExtract::NOverwriteMode::kRenameExisting);
            QCOMPARE(dialog.Password, QStringLiteral("секрет с пробелом"));
            QVERIFY(QDir::fromNativeSeparators(dialog.destination()).endsWith('/'));
        }
        CExtractDialog restarted(dir_.path(), {});
        QCOMPARE(pathMode(restarted)->currentIndex(), 1);
        QCOMPARE(overwrite(restarted)->currentIndex(), 4);
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
        QCOMPARE(overwrite(afterCancel)->currentIndex(), 4);
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
        settings.setValue("Extraction/ExtractMode", UInt32(NExtract::NPathMode::kAbsPaths));
        settings.sync();
        CExtractDialog absolute("/initial", {});
        absolute.accept();
        QCOMPARE(absolute.PathMode, NExtract::NPathMode::kAbsPaths);
    }
    void realExtractionUsesDialogModes_data() {
        QTest::addColumn<int>("pathModeIndex");
        QTest::newRow("full-paths") << 0;
        QTest::newRow("no-paths") << 1;
    }
    void realExtractionUsesDialogModes() {
        QFETCH(int, pathModeIndex);
        const QString leaf = QStringLiteral("файл с пробелом.txt");
        ArchiveBackend backend;
        const QString archive = dir_.filePath("fixture.zip");
        QFile::remove(archive);
        NativeArchiveFixture::Item item;
        item.Path = "nested/" + leaf;
        item.Bytes = "archive content";
        QVERIFY(NativeArchiveFixture::Write(archive, {item}, "zip"));
        const QString output =
            dir_.filePath(QStringLiteral("папка извлечения %1").arg(pathModeIndex));
        const QString relative = pathModeIndex == 0 ? "nested/" + leaf : leaf;
        QFile existing(QDir(output).filePath(relative));
        QVERIFY(QDir().mkpath(QFileInfo(existing).absolutePath()));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("existing"), qint64(8));
        existing.close();
        const auto extract = [&](int overwriteIndex) {
            return ExtractDialogFixture::Run(backend, archive, output, [&](CExtractDialog &dialog) {
                path(dialog)->setEditText(output);
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
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ExtractDialogTests)
#include "extract_dialog_tests.moc"
