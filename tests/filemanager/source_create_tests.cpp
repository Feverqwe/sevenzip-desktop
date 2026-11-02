#include "7zip/UI/FileManager/resource.h"
#include "dialogs/combo/ComboDialog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class SourceCreateTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static int row(FileTable *view, const QString &name) {
        for (int i = 0; i < view->rowCount(); ++i)
            if (view->model()->index(i, 0).data(Qt::UserRole + 3).toString() == name)
                return i;
        return -1;
    }
  private slots:
    void invalidDraftCanBeCorrected_data() {
        QTest::addColumn<bool>("file");
        QTest::newRow("file") << true;
        QTest::newRow("folder") << false;
    }
    void invalidDraftCanBeCorrected() {
        QFETCH(bool, file);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 0);
        view->setFocus();
        const QString finalName = QStringLiteral("corrected 空");
        bool rejected = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CComboDialog *>("comboDialog");
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *combo = dialog->findChild<QComboBox *>("comboValue");
            combo->setCurrentText(QStringLiteral("truncated") + QChar::Null + "suffix");
            bool error = false;
            QTimer::singleShot(0, dialog, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    error = !box->text().isEmpty();
                    box->accept();
                }
            });
            dialog->accept();
            QVERIFY(error);
            QVERIFY(dialog->isVisible());
            QVERIFY(!QFileInfo::exists(fixture.filePath("truncated")));
            QCOMPARE(view->rowCount(), 0);
            rejected = true;
            combo->setCurrentText(finalName);
            dialog->accept();
        });
        action(window, file ? "createFileAction" : "createFolderAction")->trigger();
        QVERIFY(rejected);
        const QFileInfo created(fixture.filePath(finalName));
        QVERIFY(created.exists());
        QCOMPARE(created.isFile(), file);
        QVERIFY(!QFileInfo::exists(fixture.filePath("truncated")));
        QTRY_COMPARE(view->rowCount(), 1);
        QCOMPARE(view->currentRow(), row(view, finalName));
    }

    void create_data() {
        QTest::addColumn<bool>("file");
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("alternative");
        QTest::addColumn<bool>("flat");
        for (bool file : {false, true})
            for (unsigned pane : {0u, 1u})
                for (const QString language : {"-", "ru"})
                    for (bool alternative : {false, true})
                        QTest::newRow(qPrintable(QString("%1-%2-%3-%4")
                                                     .arg(file)
                                                     .arg(pane)
                                                     .arg(language)
                                                     .arg(alternative)))
                            << file << pane << language << alternative << false;
        for (bool file : {false, true})
            for (unsigned pane : {0u, 1u})
                for (const QString language : {"-", "ru"})
                    QTest::newRow(
                        qPrintable(QString("flat-%1-%2-%3").arg(file).arg(pane).arg(language)))
                        << file << pane << language << false << true;
    }
    void create() {
        QFETCH(bool, file);
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(bool, alternative);
        QFETCH(bool, flat);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.AlternativeSelection = alternative;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QTemporaryDir peerDirectory;
        QVERIFY(fixture.isValid());
        QVERIFY(peerDirectory.isValid());
        QFile keep(fixture.filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        if (file)
            QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("nested 空")));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        QTRY_COMPARE(view->rowCount(), file ? 2 : 1);
        action(window, "flatViewAction")->setChecked(flat);
        QTRY_COMPARE(view->rowCount(), file ? 2 : 1);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->selectRow(row(view, "keep.bin"));
        view->setCurrentCell(row(view, "keep.bin"), 0);
        if (alternative)
            view->sourcePanel->SelectSpec(L"keep.bin", true);
        view->setFocus();
        const auto initial = view->selectionModel()->selection();
        const QString entered =
            file ? QStringLiteral("nested 空/new 文件.txt") : QStringLiteral("new folder 空/child");
        QString title, prompt, defaultName;
        bool observed = false;
        bool reports = true;
        const auto invoke = [&](bool accept) {
            observed = false;
            QTimer::singleShot(0, &window, [&] {
                auto *dialog = window.findChild<CComboDialog *>("comboDialog");
                if (!dialog)
                    return;
                observed = true;
                title = dialog->windowTitle();
                prompt = dialog->findChild<QLabel *>("comboPrompt")->text();
                defaultName = dialog->findChild<QComboBox *>("comboValue")->currentText();
                for (int width : {360, 260}) {
                    dialog->resize(width, 150);
                    QApplication::processEvents();
                    reports &= UiInspection::writeReport(
                        *dialog,
                        QString("source-create-%1-%2").arg(QTest::currentDataTag()).arg(width));
                    reports &= UiInspection::fullyVisible(
                        *dialog->findChild<QComboBox *>("comboValue"), *dialog);
                }
                dialog->findChild<QComboBox *>("comboValue")->setEditText(entered);
                if (accept)
                    dialog->accept();
                else
                    dialog->reject();
            });
            action(window, file ? "createFileAction" : "createFolderAction")->trigger();
        };
        invoke(false);
        QVERIFY(observed);
        QCOMPARE(view->selectionModel()->selection(), initial);
        QVERIFY(!QFileInfo::exists(fixture.filePath(entered)));
        QCOMPARE(title, ToQString(LangString(file ? IDS_CREATE_FILE : IDS_CREATE_FOLDER)));
        QCOMPARE(prompt,
                 ToQString(LangString(file ? IDS_CREATE_FILE_NAME : IDS_CREATE_FOLDER_NAME)));
        QCOMPARE(defaultName, ToQString(LangString(file ? IDS_CREATE_FILE_DEFAULT_NAME
                                                        : IDS_CREATE_FOLDER_DEFAULT_NAME)));
        QVERIFY(reports);
        invoke(true);
        QVERIFY(observed);
        QTRY_VERIFY(QFileInfo::exists(fixture.filePath(entered)));
        QTRY_COMPARE(view->rowCount(), flat ? 3 : 2);
        const QString focused =
            file ? QStringLiteral("nested 空") : QStringLiteral("new folder 空");
        QTRY_COMPARE(view->currentRow(), row(view, focused));
        QTRY_COMPARE(view->selectionModel()->selectedRows().size(), 1);
        UStringVector selectedNames;
        view->sourcePanel->GetSelectedNames(selectedNames);
        QCOMPARE(selectedNames.Size(), 1u);
        QCOMPARE(ToQString(selectedNames[0]), alternative ? QStringLiteral("keep.bin") : focused);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peer->rowCount(), 0);
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
    void deferredRefresh_data() {
        QTest::addColumn<bool>("file");
        QTest::newRow("file") << true;
        QTest::newRow("folder") << false;
    }
    void deferredRefresh() {
        QFETCH(bool, file);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < 257; ++i) {
            QFile entry(fixture.filePath(QString("old-%1.bin").arg(i)));
            QVERIFY(entry.open(QIODevice::WriteOnly));
            QCOMPARE(entry.write("keep bytes"), qint64(10));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 257);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        const QString name = QStringLiteral("new 文件 with spaces");
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = window.findChild<CComboDialog *>("comboDialog")) {
                dialog->findChild<QComboBox *>("comboValue")->setEditText(name);
                dialog->accept();
            }
        });
        action(window, file ? "createFileAction" : "createFolderAction")->trigger();
        QVERIFY(!panel.ShouldProcessNotifications());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 258);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(), name);
        QVERIFY(QFileInfo::exists(fixture.filePath(name)));
        QFile keep(fixture.filePath("old-0.bin"));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("keep bytes"));
    }
    void failure_data() {
        QTest::addColumn<bool>("file");
        QTest::addColumn<QString>("language");
        for (bool file : {false, true})
            for (const QString language : {"-", "ru"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(file).arg(language)))
                    << file << language;
    }
    void failure() {
        QFETCH(bool, file);
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile keep(fixture.filePath("keep.bin"));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 1);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        view->setFocus();
        QString caption;
        int dialogs = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *dialog = dynamic_cast<CComboDialog *>(QApplication::activeModalWidget())) {
                const auto close = qScopeGuard([&] {
                    if (dialog->isVisible())
                        dialog->reject();
                });
                ++dialogs;
                QFile external(fixture.filePath("external.bin"));
                QVERIFY(external.open(QIODevice::WriteOnly));
                QCOMPARE(external.write("external bytes"), qint64(14));
                dialog->findChild<QComboBox *>("comboValue")->setEditText("keep.bin");
                dialog->accept();
            } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                caption = box->windowTitle();
#ifdef Q_OS_MACOS
                if (file)
                    caption = box->text();
                else if (auto *progress = dynamic_cast<CProgressDialog *>(box->parentWidget()))
                    caption = ToQString(progress->Sync.FinalMessage.ErrorMessage.Title);
#endif
                QVERIFY(!box->text().isEmpty());
                box->accept();
            }
        });
        responder.start(1);
        action(window, file ? "createFileAction" : "createFolderAction")->trigger();
        responder.stop();
        QCOMPARE(dialogs, 1);
        QCOMPARE(caption,
                 ToQString(LangString(file ? IDS_CREATE_FILE_ERROR : IDS_CREATE_FOLDER_ERROR)));
        // Source CreateFolder refreshes even after failure; CreateFile returns
        // before refresh. Both preserve the existing selected file and bytes.
        QTRY_COMPARE(view->rowCount(), file ? 1 : 2);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(view->currentRow(), row(view, "keep.bin"));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceCreateTests)
#include "source_create_tests.moc"
