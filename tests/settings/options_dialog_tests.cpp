#include "Common/StringConvert.h"
#include "platform/temp/WorkDir.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

extern bool g_DisableUserQuestions;

namespace {
QString qtPath(const FString &path) { return QString::fromWCharArray(fs2us(path)); }
class DestroyOnEnable final : public QObject {
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::EnabledChange && static_cast<QWidget *>(object)->isEnabled()) {
            delete object;
            return true;
        }
        return false;
    }
};
} // namespace

class OptionsDialogTests : public QObject {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
  private slots:
    void settingsGeometryAndKeyboardSpin() {
        QTemporaryDir fixture;
        QtFmOptionsDialog dialog(fixture.filePath("options.ini"));
        OptionsPresentation::show(dialog, "extractMemorySpin");
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        for (QSize size : {QSize(640, 390), QSize(520, 380), QSize(640, 720), QSize(900, 800)}) {
            dialog.resize(size);
            QApplication::processEvents();
            // Native resize constraints and height-for-width layouts can
            // change the requested size. Check the actual content below.
            QWidget *previous = nullptr;
            for (const char *name :
                 {"showDotsOption", "realFileIconsOption", "fullRowOption", "gridOption",
                  "singleClickOption", "alternativeSelectionOption", "extractMemoryLabel",
                  "extractMemorySpin"}) {
                auto *control = dialog.findChild<QWidget *>(name);
                QVERIFY(control);
                OptionsPresentation::show(dialog, control);
                QVERIFY(UiInspection::fullyVisible(*control, dialog));
                if (previous && previous->parentWidget() == control->parentWidget())
                    QVERIFY(UiInspection::separate(*previous, *control, dialog));
                previous = control;
            }
            auto *spin = dialog.findChild<QWidget *>("extractMemorySpin");
            QLabel *label = nullptr;
            for (auto *candidate : dialog.findChildren<QLabel *>())
                if (candidate->buddy() == spin)
                    label = candidate;
            QVERIFY(label);
            // Growing the sheet must leave free space below the settings,
            // rather than stretching the label and separating its input row.
            QVERIFY(label->height() <= label->heightForWidth(label->width()));
            const int memoryGap = UiInspection::bounds(*spin, dialog).top() -
                                  UiInspection::bounds(*label, dialog).bottom();
            QVERIFY(memoryGap <= 2 * label->fontMetrics().lineSpacing());
            auto *ram = dialog.findChild<QWidget *>("extractMemoryRam");
            auto *enabled = dialog.findChild<QCheckBox *>("extractMemorySet");
            QVERIFY(UiInspection::writeReport(dialog, "settings-memory-review"));
            QVERIFY(UiInspection::fullyVisible(*ram, dialog));
            QVERIFY(UiInspection::separate(*enabled, *spin, dialog));
            QVERIFY(UiInspection::separate(*spin, *ram, dialog));
            auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
            QVERIFY(UiInspection::separate(*spin, *buttons, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog, QString("options-settings-%1x%2").arg(size.width()).arg(size.height())));
        }
        dialog.findChild<QCheckBox *>("extractMemorySet")->setChecked(true);
        auto *edit = dialog.findChild<QLineEdit *>("extractMemoryEdit");
        edit->setText("1");
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_Down);
        QCOMPARE(edit->text(), QString("1"));
        edit->setText("16385");
        QTest::keyClick(edit, Qt::Key_Up);
        QCOMPARE(edit->text(),
                 QString("16385")); // Above arrow range stays valid typed source input.
    }
    void foldersBrowseCancelPreservesPath() {
        QTemporaryDir fixture;
        QtFmOptionsDialog dialog(fixture.filePath("options.ini"));
        OptionsPresentation::show(dialog, "workSystem");
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        dialog.findChild<QRadioButton *>("workSpecified")->click();
        auto *path = dialog.findChild<QLineEdit *>("workPath");
        path->setText(fixture.path());
        bool browsed = false;
        widgetDialogs_.respond(dialog, [&](QDialog &active) {
            auto *browse = qobject_cast<QFileDialog *>(&active);
            QVERIFY(browse);
            browsed = true;
            browse->reject();
        });
        dialog.findChild<QPushButton *>("workPathBrowse")->click();
        QVERIFY(browsed);
        QCOMPARE(path->text(), fixture.path());
    }
    void sourceMemoryValidation_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<bool>("disabled");
        QTest::newRow("empty") << QString() << false << false;
        QTest::newRow("trailing") << QString("3GB") << false << false;
        QTest::newRow("negative") << QString("-1") << false << false;
        QTest::newRow("over-source-limit") << QString::number((1u << 30) + 1) << false << false;
        QTest::newRow("zero-source-accepted") << QString("0") << true << false;
        QTest::newRow("source-maximum") << QString::number(1u << 30) << true << false;
        QTest::newRow("above-arrow-range") << QString("16385") << true << false;
        QTest::newRow("suppressed-empty") << QString() << false << true;
    }
    void sourceMemoryValidation() {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(bool, disabled);
        QScopedValueRollback<bool> questions(g_DisableUserQuestions, disabled);
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("memory.ini");
        QtFmOptionsDialog dialog(settings);
        dialog.resize(text.isEmpty() ? QSize(520, 380) : QSize(640, 390));
        dialog.show();
        auto *enabled = dialog.findChild<QCheckBox *>("extractMemorySet");
        auto *edit = dialog.findChild<QLineEdit *>("extractMemoryEdit");
        QVERIFY(!enabled->isChecked() && !edit->isEnabled());
        enabled->setChecked(true);
        edit->setText(text);
        dialog.findChild<QCheckBox *>("showDotsOption")->setChecked(true);
        bool errorShown = false;
        QMessageBox::Icon errorIcon = QMessageBox::NoIcon;
        QString errorTitle;
        if (!valid)
            widgetDialogs_.respond(dialog, [&](QDialog &active) {
                auto *error = qobject_cast<QMessageBox *>(&active);
                QVERIFY(error);
                errorShown = true;
                errorIcon = error->icon();
                errorTitle = error->windowTitle();
                const bool report = UiInspection::writeReport(
                    *error, QString("options-memory-error-%1").arg(QTest::currentDataTag()));
                error->accept();
                QVERIFY(report);
            });
        dialog.findChild<QDialogButtonBox *>("settingsButtons")
            ->button(QDialogButtonBox::Ok)
            ->click();
        QCOMPARE(dialog.isVisible(), !valid);
        QCOMPARE(errorShown, !valid && !disabled);
        if (errorShown) {
            QCOMPARE(errorIcon, QMessageBox::Critical);
#ifndef Q_OS_MACOS
            QCOMPARE(errorTitle, QString("7-Zip"));
#endif
        }
        QCOMPARE(NExtract::Read_LimitGB(settings), valid ? text.toUInt() : UInt32(-1));
        CFmSettings general;
        general.Load(settings);
        QVERIFY(general.ShowDots); // Source saves general flags before memory validation.
        if (!valid) {
            auto *focused = dialog.focusWidget();
            QVERIFY2(focused == edit || focused == dialog.findChild<QWidget *>("extractMemorySpin"),
                     focused ? qPrintable(focused->objectName()) : "No pending focus widget");
            QVERIFY(dialog.findChild<QDialogButtonBox *>("settingsButtons")
                        ->button(QDialogButtonBox::Apply)
                        ->isEnabled());
            QCOMPARE(edit->text(), text);
            edit->setText("2");
            dialog.findChild<QDialogButtonBox *>("settingsButtons")
                ->button(QDialogButtonBox::Ok)
                ->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(2));
        }
    }
    void appliedObserverCanDestroyDialog_data() {
        QTest::addColumn<int>("finish");
        QTest::newRow("apply") << 0;
        QTest::newRow("ok") << 1;
        QTest::newRow("ok-enable-after-applied") << 2;
    }
    void appliedObserverCanDestroyDialog() {
        QFETCH(int, finish);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("deleted-dialog.ini");
        QPointer<QtFmOptionsDialog> dialog = new QtFmOptionsDialog(settings);
        const auto cleanup = qScopeGuard([&] { delete dialog.data(); });
        const QString command = QStringLiteral("editor with spaces --open");
        dialog->findChild<QLineEdit *>("editorCommand")->setText(command);
        DestroyOnEnable destroyOnEnable;
        connect(dialog, &QtFmOptionsDialog::Applied, this, [&] {
            if (finish == 2) {
                dialog->setEnabled(false);
                dialog->installEventFilter(&destroyOnEnable);
            } else
                delete dialog.data();
        });
        if (finish != 0)
            dialog->accept();
        else
            QVERIFY(!dialog->apply());
        QVERIFY(dialog.isNull());
        UString saved;
        ReadRegEditor(true, saved, settings);
        QCOMPARE(QString::fromWCharArray(saved.Ptr()), command);
    }
    void appliedObserverDraftIsNotDiscardedByOk_data() {
        QTest::addColumn<bool>("memory");
        QTest::newRow("memory") << true;
        QTest::newRow("editor") << false;
    }
    void appliedObserverDraftIsNotDiscardedByOk() {
        QFETCH(bool, memory);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("pending.ini");
        const QString initial = memory ? "3" : "original editor";
        const QString draft = memory ? "7" : "new editor --open";
        NExtract::Save_LimitGB(settings, 3);
        if (!memory)
            SaveRegEditor(true, UString(initial.toStdWString().c_str()), settings);
        const auto savedValue = [&] {
            if (memory)
                return QString::number(NExtract::Read_LimitGB(settings));
            UString saved;
            ReadRegEditor(true, saved, settings);
            return QString::fromWCharArray(saved.Ptr());
        };
        QtFmOptionsDialog dialog(settings);
        dialog.show();
        const char *control = memory ? "extractMemoryEdit" : "editorCommand";
        QVERIFY(OptionsPresentation::show(dialog, control));
        auto *edit = dialog.findChild<QLineEdit *>(control);
        auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        bool changed = false;
        connect(&dialog, &QtFmOptionsDialog::Applied, &dialog, [&] {
            if (!changed) {
                changed = true;
                edit->setText(draft);
            }
        });
        buttons->button(QDialogButtonBox::Ok)->click();
        QVERIFY(changed);
        QVERIFY(dialog.isVisible());
        QVERIFY(buttons->button(QDialogButtonBox::Apply)->isEnabled());
        QCOMPARE(edit->text(), draft);
        QCOMPARE(savedValue(), initial);
        buttons->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        QCOMPARE(savedValue(), draft);
    }

    void memoryApplyThenCancelAndUnset() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("memory.ini");
        NExtract::Save_LimitGB(settings, 3);
        QtFmOptionsDialog dialog(settings);
        auto *edit = dialog.findChild<QLineEdit *>("extractMemoryEdit");
        QCOMPARE(edit->text(), QString("3"));
        QVERIFY(edit->isEnabled());
        edit->setText("7");
        dialog.apply();
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(7));
        edit->setText("9");
        dialog.reject();
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(7));
        QtFmOptionsDialog unset(settings);
        unset.findChild<QCheckBox *>("extractMemorySet")->setChecked(false);
        unset.accept();
        QCOMPARE(NExtract::Read_LimitGB(settings), UInt32(-1));
        QtFmOptionsDialog reloaded(settings);
        QVERIFY(!reloaded.findChild<QCheckBox *>("extractMemorySet")->isChecked());
        QCOMPARE(reloaded.findChild<QLineEdit *>("extractMemoryEdit")->text(), QString("4"));
    }
    void foldersApplyCancelAndRealWorkDirectory_data() {
        QTest::addColumn<bool>("apply");
        QTest::newRow("cancel") << false;
        QTest::newRow("apply-then-cancel") << true;
    }
    void foldersApplyCancelAndRealWorkDirectory() {
        QFETCH(bool, apply);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("options.ini");
        NWorkDir::CInfo old;
        old.SetDefault();
        old.Save(settings);
        QtFmOptionsDialog dialog(settings);
        auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
        OptionsPresentation::show(dialog, "workSystem");
        auto *path = dialog.findChild<QLineEdit *>("workPath");
        auto *browse = dialog.findChild<QPushButton *>("workPathBrowse");
        QVERIFY(!path->isEnabled() && !browse->isEnabled());
        auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        auto *applyButton = buttons->button(QDialogButtonBox::Apply);
        QVERIFY(!applyButton->isEnabled());
        dialog.show();
        auto *specified = dialog.findChild<QRadioButton *>("workSpecified");
        specified->setFocus();
        QTest::keyClick(specified, Qt::Key_Space);
        QVERIFY(path->isEnabled() && browse->isEnabled());
        const QString work = fixture.filePath(QStringLiteral("рабочая папка"));
        path->setText(work);
        QVERIFY(applyButton->isEnabled());
        if (apply) {
            applyButton->click();
            QVERIFY(!applyButton->isEnabled());
            path->setText(fixture.filePath("discarded"));
        }
        dialog.reject();
        NWorkDir::CInfo loaded;
        loaded.Load(settings);
        QCOMPARE(loaded.Mode, apply ? NWorkDir::NMode::kSpecified : NWorkDir::NMode::kSystem);
        QCOMPARE(qtPath(loaded.Path), apply ? work : QString());
        if (apply) {
            const QByteArray previous = qgetenv("SEVENZIP_SETTINGS_PATH");
            qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
            const auto restore = qScopeGuard([&] {
                if (previous.isNull())
                    qunsetenv("SEVENZIP_SETTINGS_PATH");
                else
                    qputenv("SEVENZIP_SETTINGS_PATH", previous);
            });
            QString temporary;
            {
                CWorkDirTempFile file;
                QCOMPARE(file.CreateTempFile(
                             us2fs(fixture.filePath("archive.zip").toStdWString().c_str())),
                         HRESULT(S_OK));
                temporary = qtPath(file.Get_TempFilePath());
                QVERIFY(QDir::fromNativeSeparators(temporary).startsWith(
                    QDir::fromNativeSeparators(work) + '/'));
                QVERIFY(QFileInfo::exists(temporary));
            }
            QVERIFY(!QFileInfo::exists(temporary));
        }
    }
    void malformedWorkPathCanBeCorrectedOrCancelled_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("cancel");
        for (const QSize size : {QSize(640, 390), QSize(520, 380)})
            for (int mode = 0; mode < 2; ++mode)
                for (bool cancel : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(size.width()).arg(mode).arg(cancel)))
                        << size << mode << cancel;
    }
    void malformedWorkPathCanBeCorrectedOrCancelled() {
        QFETCH(QSize, size);
        QFETCH(int, mode);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString settings = fixture.filePath("options.ini");
        NWorkDir::CInfo old;
        old.SetDefault();
        old.Save(settings);
        QtFmOptionsDialog dialog(settings);
        dialog.resize(size);
        OptionsPresentation::show(dialog, "workSpecified");
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        dialog.findChild<QRadioButton *>("workSpecified")->click();
        auto *path = dialog.findChild<QLineEdit *>("workPath");
        const QString shortened = fixture.filePath(QStringLiteral("shortened рабочая папка"));
        const QString invalid = shortened + QChar::Null + "/lost suffix";
        path->setText(invalid);
        int errors = 0;
        const auto answerError = [&] {
            widgetDialogs_.respond(dialog, [&](QDialog &active) {
                auto *error = qobject_cast<QMessageBox *>(&active);
                active.reject();
                QVERIFY(error);
                QCOMPARE(error->icon(), QMessageBox::Critical);
                QVERIFY(!error->text().isEmpty());
                ++errors;
            });
        };
        answerError();
        dialog.findChild<QPushButton *>("workPathBrowse")->click();
        QCOMPARE(errors, 1);
        const char *modes[] = {"workSystem", "workSpecified"};
        dialog.findChild<QRadioButton *>(modes[mode])->click();
        OptionsPresentation::show(dialog, "gridOption");
        auto *apply = dialog.findChild<QDialogButtonBox *>("settingsButtons")
                          ->button(QDialogButtonBox::Apply);
        QSignalSpy applied(&dialog, &QtFmOptionsDialog::Applied);
        answerError();
        dialog.accept();
        QCOMPARE(errors, 2);
        QCOMPARE(applied.size(), 0);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(apply->isEnabled());
        QCOMPARE(path->text(), invalid);
        auto *modeControl = dialog.findChild<QRadioButton *>(modes[mode]);
        QVERIFY(modeControl->isChecked());
        auto *correction =
            mode == 1 ? static_cast<QWidget *>(path) : dialog.findChild<QWidget *>("workSpecified");
        QVERIFY(correction->isEnabled() && correction->isVisible());
        QCOMPARE(dialog.focusWidget(), correction);
        NWorkDir::CInfo loaded;
        loaded.Load(settings);
        QCOMPARE(loaded.Mode, old.Mode);
        QVERIFY(loaded.Path.IsEmpty());
        QVERIFY(!QFileInfo::exists(shortened));
        const QString corrected = fixture.filePath(QStringLiteral("corrected рабочая папка 空"));
        if (cancel)
            dialog.reject();
        else {
            dialog.findChild<QRadioButton *>("workSpecified")->click();
            path->setText(corrected);
            dialog.accept();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(applied.size(), 1);
            CWorkDirTempFile file;
            QCOMPARE(file.CreateTempFile(
                         us2fs(fixture.filePath("archive.zip").toStdWString().c_str()), settings),
                     HRESULT(S_OK));
            QVERIFY(QDir::fromNativeSeparators(qtPath(file.Get_TempFilePath()))
                        .startsWith(corrected + '/'));
        }
        loaded.Load(settings);
        QCOMPARE(loaded.Mode, cancel ? old.Mode : NWorkDir::NMode::kSpecified);
        QCOMPARE(qtPath(loaded.Path), cancel ? QString() : corrected);
        QVERIFY(!QFileInfo::exists(shortened));
    }

    void foldersGeometryAndInvalidConfiguration() {
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("options.ini");
        {
            QSettings corrupt(settings, QSettings::IniFormat);
            corrupt.setValue("Options/WorkDirType", "invalid");
            corrupt.setValue("Options/WorkDirPath", "ignored");
        }
        QtFmOptionsDialog dialog(settings);
        QVERIFY(dialog.findChild<QRadioButton *>("workSystem")->isChecked());
        OptionsPresentation::show(dialog, "workSystem");
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        for (QSize size : {QSize(640, 390), QSize(520, 380)}) {
            dialog.resize(size);
            QApplication::processEvents();
            OptionsPresentation::show(dialog, "workPathBrowse");
            QWidget *previous = nullptr;
            for (const char *name : {"workSystem", "workSpecified", "workPath"}) {
                auto *control = dialog.findChild<QWidget *>(name);
                OptionsPresentation::show(dialog, control);
                QVERIFY(control && UiInspection::fullyVisible(*control, dialog));
                if (previous)
                    QVERIFY2(
                        UiInspection::bounds(*previous, dialog).bottom() <
                            UiInspection::bounds(*control, dialog).top(),
                        qPrintable(previous->objectName() + " before " + control->objectName()));
                previous = control;
            }
            auto *path = dialog.findChild<QLineEdit *>("workPath");
            auto *browse = dialog.findChild<QPushButton *>("workPathBrowse");
            OptionsPresentation::show(dialog, browse);
            QVERIFY(UiInspection::writeReport(dialog, "settings-folders-review"));
            QVERIFY(UiInspection::fullyVisible(*browse, dialog));
            QVERIFY(UiInspection::separate(*path, *browse, dialog));
            QVERIFY(UiInspection::fullyVisible(
                *dialog.findChild<QDialogButtonBox *>("settingsButtons"), dialog));
            QVERIFY(
                UiInspection::writeReport(dialog, QString("options-folders-%1").arg(size.width())));
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(OptionsDialogTests)
#include "options_dialog_tests.moc"
