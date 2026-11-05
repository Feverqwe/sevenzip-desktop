#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "settings/EditPage.h"
#include "settings/RegistryUtils.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTabWidget>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes = "contents") {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QString command(const QString &output) {
    return QString("\"%1\" --external-fixture \"%2\"")
        .arg(QCoreApplication::applicationFilePath(), output);
}
QStringList readArguments(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QStringList result;
    for (const auto &value : QJsonDocument::fromJson(file.readAll()).array())
        result.append(
            QDir::fromNativeSeparators(value.toString()).normalized(QString::NormalizationForm_C));
    return result;
}
void navigate(MainWindow &window, const QString &path) {
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(path);
    QTest::keyClick(address, Qt::Key_Return);
}
} // namespace

class ExternalEditorsUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
    }

    void editorPageApplyCancelAndGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("English") << QString("-");
        QTest::newRow("Russian") << QString("ru");
    }
    void editorPageApplyCancelAndGeometry() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        SaveRegEditor(false, UString(L"viewer --old"), settings);
        for (bool apply : {false, true}) {
            QtFmOptionsDialog dialog(settings);
            auto *applyButton = dialog.findChild<QDialogButtonBox *>("settingsButtons")
                                    ->button(QDialogButtonBox::Apply);
            QVERIFY(!applyButton->isEnabled());
            auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
            OptionsPresentation::show(dialog, "viewerCommand");
            auto *confirm = dialog.findChild<QCheckBox *>("confirmUnknownFileOpenOption");
            QVERIFY(confirm->isChecked());
            confirm->setChecked(false);
            dialog.findChild<QLineEdit *>("viewerCommand")->setText("new viewer");
            dialog.findChild<QLineEdit *>("editorCommand")->setText("editor --edit");
            dialog.findChild<QLineEdit *>("diffCommand")->setText("diff --compare");
            QVERIFY(applyButton->isEnabled());
            dialog.show();
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            for (QSize size : {QSize(640, 390), QSize(520, 380)}) {
                dialog.resize(size);
                QApplication::processEvents();
                for (const char *name : {"viewerCommand", "editorCommand", "diffCommand"}) {
                    auto *edit = dialog.findChild<QLineEdit *>(name);
                    OptionsPresentation::show(dialog, edit);
                    auto *browse = dialog.findChild<QPushButton *>(QString(name) + "Browse");
                    OptionsPresentation::show(dialog, browse);
                    QVERIFY(UiInspection::fullyVisible(*edit, dialog));
                    QVERIFY(UiInspection::fullyVisible(*browse, dialog));
                    QVERIFY(UiInspection::separate(*edit, *browse, dialog));
                }
                QVERIFY(UiInspection::fullyVisible(
                    *dialog.findChild<QDialogButtonBox *>("settingsButtons"), dialog));
                OptionsPresentation::show(dialog, confirm);
                QVERIFY(UiInspection::fullyVisible(*confirm, dialog));
                QVERIFY(confirm->width() >= confirm->sizeHint().width());
                QVERIFY(UiInspection::writeReport(
                    dialog,
                    QString("external-editor-options-%1-%2").arg(language).arg(size.width())));
            }
            if (apply) {
                dialog.findChild<QDialogButtonBox *>("settingsButtons")
                    ->button(QDialogButtonBox::Apply)
                    ->click();
                QVERIFY(!applyButton->isEnabled());
                QVERIFY(dialog.isVisible());
                dialog.findChild<QLineEdit *>("viewerCommand")->setText("discarded after Apply");
            }
            dialog.reject();
            QCOMPARE(ReadConfirmUnknownFileOpen(settings), !apply);
            UString value;
            ReadRegEditor(false, value, settings);
            QCOMPARE(ToQString(value), apply ? QString("new viewer") : QString("viewer --old"));
            ReadRegDiff(value, settings);
            QCOMPARE(ToQString(value), apply ? QString("diff --compare") : QString());
        }
    }

    void editorBrowsePreservesCancelAndReplacesParameters_data() {
        QTest::addColumn<QString>("control");
        QTest::addColumn<int>("program");
        QTest::newRow("viewer") << QString("viewerCommand") << 0;
        QTest::newRow("editor") << QString("editorCommand") << 1;
        QTest::newRow("diff") << QString("diffCommand") << 2;
    }
    void editorBrowsePreservesCancelAndReplacesParameters() {
        QFETCH(QString, control);
        QFETCH(int, program);
        QTemporaryDir fixture;
        const QString selected = fixture.filePath(QStringLiteral("программа с пробелом")
#ifdef Q_OS_WIN
                                                  + ".exe"
#endif
        );
        QVERIFY(writeFile(selected));
        CEditPage page(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        auto *edit = page.findChild<QLineEdit *>(control);
        auto *browse = page.findChild<QPushButton *>(control + "Browse");
        const QString original = "\"" + selected + "\" --old-parameter";
        edit->setText(original);
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        browse->click();
        QCOMPARE(edit->text(), original);
        QTimer::singleShot(0, [&] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->selectFile(selected);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        browse->click();
        QCOMPARE(edit->text(), selected);
        page.OnApply();
        UString saved;
        if (program < 2)
            ReadRegEditor(program > 0, saved, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        else
            ReadRegDiff(saved, qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QCOMPARE(ToQString(saved), selected);
    }

    void malformedCommandCanBeCorrectedOrCancelledBeforeLaunch_data() {
        QTest::addColumn<int>("program");
        QTest::addColumn<bool>("cancel");
        for (int program = 0; program < 3; ++program)
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(program).arg(cancel)))
                    << program << cancel;
    }
    void malformedCommandCanBeCorrectedOrCancelledBeforeLaunch() {
        QFETCH(int, program);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString first = fixture.filePath("a.txt");
        const QString second = fixture.filePath(QStringLiteral("файл 空 с пробелом.txt"));
        QVERIFY(writeFile(first) && writeFile(second));
        const QString oldOutput = fixture.filePath("old.json");
        const QString newOutput = fixture.filePath("corrected.json");
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const QString oldCommand = command(oldOutput);
        const auto readCommand = [&] {
            UString value;
            if (program < 2)
                ReadRegEditor(program > 0, value, settings);
            else
                ReadRegDiff(value, settings);
            return ToQString(value);
        };
        if (program < 2)
            SaveRegEditor(program > 0, ToUString(oldCommand), settings);
        else
            SaveRegDiff(ToUString(oldCommand), settings);
        WidgetDialogFixture dialogs;
        QtFmOptionsDialog options(settings);
        options.resize(520, 380);
        const char *controls[] = {"viewerCommand", "editorCommand", "diffCommand"};
        OptionsPresentation::show(options, controls[program]);
        options.show();
        QVERIFY(QTest::qWaitForWindowActive(&options));
        auto *edit = options.findChild<QLineEdit *>(controls[program]);
        const QString corrected = command(newOutput);
        const QString invalid = corrected + QChar::Null + " ignored suffix";
        edit->setText(invalid);
        int errors = 0;
        const auto answerError = [&] {
            dialogs.respond(options, [&](QDialog &active) {
                auto *error = qobject_cast<QMessageBox *>(&active);
                active.reject();
                QVERIFY(error);
                QCOMPARE(error->icon(), QMessageBox::Critical);
                QVERIFY(!error->text().isEmpty());
                ++errors;
            });
        };
        answerError();
        options.findChild<QPushButton *>(QString(controls[program]) + "Browse")->click();
        QCOMPARE(errors, 1);
        OptionsPresentation::show(options, "gridOption");
        QSignalSpy applied(&options, &QtFmOptionsDialog::Applied);
        answerError();
        options.accept();
        QCOMPARE(errors, 2);
        QVERIFY(options.isVisible());
        QCOMPARE(applied.size(), 0);
        QCOMPARE(edit->text(), invalid);
        QCOMPARE(readCommand(), oldCommand);
        QVERIFY(edit->isVisible());
        QCOMPARE(options.focusWidget(), edit);
        QVERIFY(options.findChild<QDialogButtonBox *>("settingsButtons")
                    ->button(QDialogButtonBox::Apply)
                    ->isEnabled());
        if (cancel)
            options.reject();
        else {
            edit->setText(corrected);
            options.accept();
            QCOMPARE(options.result(), int(QDialog::Accepted));
            QCOMPARE(applied.size(), 1);
        }
        QCOMPARE(readCommand(), cancel ? oldCommand : corrected);
        QVERIFY(!QFileInfo::exists(oldOutput) && !QFileInfo::exists(newOutput));
        ControlledBackend backend;
        MainWindow window(&backend);
        navigate(window, fixture.path());
        window.show();
        QTRY_VERIFY(filesystemRow(window, QFileInfo(second).fileName()) >= 0);
        auto *view = table(window);
        view->selectAll();
        view->setCurrentCell(filesystemRow(window, QFileInfo(second).fileName()), 0,
                             QItemSelectionModel::NoUpdate);
        const char *actions[] = {"viewFileAction", "editFileAction", "diffAction"};
        QTRY_VERIFY(action(window, actions[program])->isEnabled());
        action(window, actions[program])->trigger();
        QStringList expected = program == 2 ? QStringList{first, second} : QStringList{second};
        expected.sort();
        const QString output = cancel ? oldOutput : newOutput;
        QTRY_COMPARE(([&] {
                         auto values = readArguments(output);
                         values.sort();
                         return values;
                     })(),
                     expected);
        QVERIFY(!QFileInfo::exists(cancel ? newOutput : oldOutput));
    }

    void keyboardDiskEditorsUseFocusedFileAndIndependentSettings() {
        QTemporaryDir fixture;
        const QString first = fixture.filePath(QStringLiteral("первый файл.txt"));
        const QString second = fixture.filePath(QStringLiteral("второй файл.txt"));
        QVERIFY(writeFile(first) && writeFile(second));
        const QString viewerOutput = fixture.filePath("viewer.json");
        const QString editorOutput = fixture.filePath("editor.json");
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegEditor(false, ToUString(command(viewerOutput)), settings);
        SaveRegEditor(true, ToUString(command(editorOutput)), settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        navigate(window, fixture.path());
        window.show();
        window.activateWindow();
        QTRY_COMPARE(QApplication::activeWindow(), &window);
        QTRY_VERIFY(filesystemRow(window, QStringLiteral("второй файл.txt")) >= 0);
        auto *view = table(window);
        view->selectAll();
        const int focused = filesystemRow(window, QStringLiteral("второй файл.txt"));
        view->setCurrentCell(focused, 0, QItemSelectionModel::NoUpdate);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        QTRY_VERIFY(action(window, "viewFileAction")->isEnabled());
        QTest::keyClick(view, Qt::Key_F3);
        QTRY_COMPARE(readArguments(viewerOutput), QStringList({second}));
        QTest::keyClick(view, Qt::Key_F4);
        QTRY_COMPARE(readArguments(editorOutput), QStringList({second}));
    }

    void diffUsesSelectedPairAndReportsLaunchFailure() {
        QTemporaryDir fixture;
        const QString first = fixture.filePath("a.txt");
        const QString second = fixture.filePath(QStringLiteral("б с пробелом.txt"));
        QVERIFY(writeFile(first) && writeFile(second));
        const QString output = fixture.filePath("diff.json");
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegDiff(ToUString(command(output)), settings);
        ControlledBackend backend;
        MainWindow window(&backend);
        navigate(window, fixture.path());
        window.show();
        QTRY_VERIFY(filesystemRow(window, "a.txt") >= 0);
        auto *view = table(window);
        view->selectAll();
        QTRY_VERIFY(action(window, "diffAction")->isEnabled());
        const int firstRow = filesystemRow(window, "a.txt");
        const int secondRow = filesystemRow(window, QStringLiteral("б с пробелом.txt"));
        const bool firstInSourceOrder =
            view->model()->index(firstRow, 0).data(Qt::UserRole + 6).toUInt() <
            view->model()->index(secondRow, 0).data(Qt::UserRole + 6).toUInt();
        const QStringList expected =
            firstInSourceOrder ? QStringList({first, second}) : QStringList({second, first});
        action(window, "diffAction")->trigger();
        QTRY_COMPARE(readArguments(output), expected);
        SaveRegDiff(ToUString(fixture.filePath("missing executable")), settings);
        bool errorShown = false;
        QTimer::singleShot(0, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errorShown = box->textFormat() == Qt::PlainText;
                box->accept();
            }
        });
        action(window, "diffAction")->trigger();
        QVERIFY(errorShown);
        QVERIFY(QFileInfo(first).isFile() && QFileInfo(second).isFile());
    }

    void diffUsesOppositeSelectionOrRelativeFallback_data() {
        QTest::addColumn<bool>("selected");
        QTest::addColumn<bool>("flat");
        QTest::newRow("opposite-selection") << true << false;
        QTest::newRow("relative-fallback") << false << false;
        QTest::newRow("flat-selection") << true << true;
        QTest::newRow("flat-basename-fallback") << false << true;
    }
    void diffUsesOppositeSelectionOrRelativeFallback() {
        QFETCH(bool, selected);
        QFETCH(bool, flat);
        QTemporaryDir fixture;
        const QString left = fixture.filePath("left");
        const QString nested = QDir(left).filePath("nested");
        const QString right = fixture.filePath("right");
        QVERIFY(QDir().mkpath(nested) && QDir().mkpath(right));
        const QString source = QDir(nested).filePath(QStringLiteral("файл с пробелом.txt"));
        const QString expected =
            QDir(right).filePath(selected ? QString("other.txt") : QFileInfo(source).fileName());
        QVERIFY(writeFile(source) && writeFile(expected));
        const QString output = fixture.filePath("diff.json");
        SaveRegDiff(ToUString(command(output)), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        navigate(window, flat ? left : nested);
        window.show();
        window.activateWindow();
        auto *leftPanel = window.findChild<QWidget *>("filePanel0");
        auto *leftTable = leftPanel->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(leftTable->rowCount() > 0);
        leftTable->setFocus();
        if (flat)
            action(window, "flatViewAction")->trigger();
        const QString displayed =
            (flat ? QString("nested/") : QString()) + QFileInfo(source).fileName();
        QTRY_VERIFY(filesystemRow(window, displayed) >= 0);
        QTRY_VERIFY(leftTable->isEnabled());
        leftTable->selectRow(filesystemRow(window, displayed));
        action(window, "twoPanelsAction")->trigger();
        QVERIFY(action(window, "twoPanelsAction")->isChecked());
        auto *rightPanel = window.findChild<QWidget *>("filePanel1");
        auto *rightTable = rightPanel->findChild<FileTable *>("archiveTable");
        auto *address = rightPanel->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(right);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(rightTable->rowCount(), 1);
        if (action(window, "flatViewAction")->isChecked())
            action(window, "flatViewAction")->trigger();
        if (selected)
            rightTable->selectRow(0);
        else
            rightTable->clearSelection();
        window.activateWindow();
        QApplication::processEvents();
        QTRY_VERIFY(leftTable->isEnabled());
        leftTable->setFocus();
        // Activating a retained flat panel can start its deferred rescan.
        // The source scan guard temporarily disables the list and drops focus.
        QTRY_VERIFY(leftTable->isEnabled());
        leftTable->setFocus();
        QTRY_VERIFY(leftTable->hasFocus());
        const auto sourceRow = [&] {
            for (int row = 0; row < leftTable->rowCount(); ++row)
                if (leftTable->model()->index(row, 0).data().toString() == displayed)
                    return row;
            return -1;
        };
        QTRY_VERIFY(sourceRow() >= 0);
        leftTable->selectRow(sourceRow());
        QTRY_VERIFY(action(window, "diffAction")->isEnabled());
        action(window, "diffAction")->trigger();
        QTRY_COMPARE(readArguments(output), QStringList({source, expected}));
    }
};

int main(int argc, char **argv) {
    if (argc >= 3 && QByteArray(argv[1]) == "--external-fixture") {
        QCoreApplication app(argc, argv);
        QFile output(app.arguments()[2]);
        if (!output.open(QIODevice::WriteOnly))
            return 1;
        const QByteArray bytes =
            QJsonDocument(QJsonArray::fromStringList(app.arguments().mid(3))).toJson();
        return output.write(bytes) == bytes.size() ? 0 : 2;
    }
    // The test drives QFileDialog's public widgets. Production still uses
    // native pickers; their OS windows are checked separately on the runner.
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    UiInspection::configureReviewAppearance();
    ExternalEditorsUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_editors_ui_tests.moc"
