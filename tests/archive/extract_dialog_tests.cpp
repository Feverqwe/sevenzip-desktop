#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/extract/alias.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
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
        QCOMPARE(dialog.options().pathMode, ExtractionOptions::PathMode::NoPaths);
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
        QCOMPARE(dialog.options().pathMode, ExtractionOptions::PathMode::FullPaths);
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
        QCOMPARE(dialog.options().pathMode, ExtractionOptions::PathMode::NoPaths);
        QCOMPARE(dialog.options().overwriteMode, ExtractionOptions::OverwriteMode::RenameExtracted);
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
        QCOMPARE(dialog.options().overwriteMode, ExtractionOptions::OverwriteMode::Ask);
        dialog.accept();
        QSettings settings(settings_, QSettings::IniFormat);
        QCOMPARE(settings.value("Toolbar/labels").toBool(), false);
        settings.setValue("Extraction/ExtractMode", UInt32(NExtract::NPathMode::kAbsPaths));
        settings.sync();
        CExtractDialog absolute("/initial", {});
        QCOMPARE(absolute.options().pathMode, ExtractionOptions::PathMode::AbsolutePaths);
    }
    void realExtractionUsesDialogModes() {
        const QString source = dir_.filePath(QStringLiteral("файл с пробелом.txt"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("archive content"), qint64(15));
        file.close();
        ArchiveBackend backend;
        QSignalSpy done(&backend, &ArchiveBackend::finished);
        const QString archive = dir_.filePath("fixture.zip");
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}));
        done.clear();
        backend.list(archive);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        done.clear();
        const QString output = dir_.filePath(QStringLiteral("папка извлечения"));
        QVERIFY(QDir().mkpath(output));
        QFile existing(QDir(output).filePath(QFileInfo(source).fileName()));
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("existing"), qint64(8));
        existing.close();
        CExtractDialog dialog(output, {});
        pathMode(dialog)->setCurrentIndex(1);
        overwrite(dialog)->setCurrentIndex(2);
        dialog.accept();
        auto options = dialog.options();
        options.ElimDup.Val = false; // This fixture exercises the basic CLI transport.
        backend.extract(archive, dialog.destination(), {}, {}, options);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("existing"));
        existing.close();
        done.clear();
        overwrite(dialog)->setCurrentIndex(1);
        options = dialog.options();
        options.ElimDup.Val = false;
        backend.extract(archive, dialog.destination(), {}, {}, options);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 15000);
        QVERIFY2(done.first()[0].toBool(), qPrintable(done.first()[1].toString()));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("archive content"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ExtractDialogTests)
#include "extract_dialog_tests.moc"
