#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTreeWidget>

class ActivationExternalFiles final : public IQtExternalFileServices {
  public:
    QStringList paths;
    HRESULT StartApplication(const UString &, const UString &path, QWidget *) override {
        paths << QDir::fromNativeSeparators(ToQString(path));
        return S_OK;
    }
};

class PanelActivationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static FileTable &list(MainWindow &window, unsigned pane) {
        return *static_cast<FileTable *>(
            window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                ->findChild<FileTable *>("archiveTable"));
    }
    static QLineEdit &address(MainWindow &window, unsigned pane) {
        return *window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                    ->findChild<QLineEdit *>("archivePath");
    }
    static void navigate(MainWindow &window, unsigned pane, const QString &path) {
        address(window, pane).setFocus();
        address(window, pane).setText(path);
        QTest::keyClick(&address(window, pane), Qt::Key_Return);
        QTRY_COMPARE(list(window, pane).rowCount(), 2);
        list(window, pane).sourcePanel->Set_AutoRefresh_Mode(false);
    }
    static void activateWithKeyboard(MainWindow &window, QWidget *control,
                                     Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        if (modifiers == Qt::NoModifier)
            QTest::keySequence(control, action(window, "openItemAction")->shortcut());
        else
            QTest::keyClick(control, Qt::Key_Return, modifiers);
    }
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; real activation workflow unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        version_ = backend.codecCatalog()->version();
#endif
    }
    void archiveOpeningFailureHasOneDialogAndCanRetry() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath("broken.pkg");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        address(window, 0).setFocus();
        address(window, 0).setText(fixture.path());
        QTest::keyClick(&address(window, 0), Qt::Key_Return);
        auto &view = list(window, 0);
        QTRY_COMPARE(view.rowCount(), 1);
        view.sourcePanel->Set_AutoRefresh_Mode(false);
        action(window, "viewMode3Action")->trigger();
        QTRY_VERIFY(view.isVisible() && view.isEnabled());
        const auto previous = view.sourcePanel->GetCurrentFolderPrefix();
        int row = 0;
        while (row < view.rowCount() &&
               view.model()->index(row, 0).data().toString() != "broken.pkg")
            ++row;
        QVERIFY(row < view.rowCount());
        view.setCurrentCell(row, 0);
        view.selectRow(row);
        view.setFocus();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          view.visualRect(view.model()->index(row, 0)).center());
        QTRY_VERIFY(action(window, "openItemAction")->isEnabled());
        backend.queueListAction([&] { backend.fail("fixture opening failure", E_FAIL); });
        unsigned errors = 0;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++errors;
                box->accept();
            }
        });
        responder.start(0);
        action(window, "openItemAction")->trigger();
        responder.stop();
        QCOMPARE(errors, 1u);
        QCOMPARE(view.sourcePanel->GetCurrentFolderPrefix(), previous);
        backend.queueList({{"payload.bin", 7, false}});
        action(window, "openItemAction")->trigger();
        QTRY_VERIFY(view.sourcePanel->IsArcFolder());
        QCOMPARE(backend.listCalls, 2);
    }
    void modifierWorkflow_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("archive");
        QTest::addColumn<bool>("mouse");
        QTest::addColumn<int>("keys");
        for (unsigned pane : {0u, 1u})
            for (bool ru : {false, true})
                for (int mode : {0, 3})
                    for (bool arc : {false, true})
                        for (bool mouse : {false, true})
                            for (int keys = 0; keys < 8; ++keys)
                                QTest::newRow(qPrintable(QString("%1-%2-%3-%4-%5-%6")
                                                             .arg(pane)
                                                             .arg(ru)
                                                             .arg(mode)
                                                             .arg(arc)
                                                             .arg(mouse)
                                                             .arg(keys)))
                                    << pane << ru << mode << arc << mouse << keys;
    }
    void modifierWorkflow() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(int, mode);
        QFETCH(bool, archive);
        QFETCH(bool, mouse);
        QFETCH(int, keys);
        const Qt::KeyboardModifiers modifiers = (keys & 1 ? Qt::ControlModifier : Qt::NoModifier) |
                                                (keys & 2 ? Qt::ShiftModifier : Qt::NoModifier) |
                                                (keys & 4 ? Qt::AltModifier : Qt::NoModifier);
        const bool properties = keys == 4;
        const bool outside = keys == 2;
#ifdef Q_OS_WIN
        if (!archive && properties)
            QSKIP("Native filesystem Properties sheet belongs to Windows shell acceptance");
#endif
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = QStringLiteral("target 文件");
        const QString files = fixture.filePath("files");
        QVERIFY(QDir().mkpath(QDir(files).filePath(target)));
        for (const QString &name : {QString("root.txt"), target + "/child δ.txt"}) {
            QFile file(QDir(files).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("untouched"), qint64(9));
        }
        const QString arc = fixture.filePath("archive with spaces.zip");
        QProcess creator;
        creator.setWorkingDirectory(files);
        creator.start(engine_, {"a", "-tzip", "--", arc, target, "root.txt"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray original = read(arc);
        QVERIFY(!original.isEmpty());
        ActivationExternalFiles external;
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        navigate(window, 1 - pane, files);
        navigate(window, pane, files);
        auto &view = list(window, pane);
        auto &panel = *view.sourcePanel;
        auto &peer = list(window, 1 - pane);
        CMyComPtr<IFolderFolder> peerProvider = peer.sourcePanel->GetFolder();
        const auto peerSelection = peer.selectionModel()->selection();
        if (archive) {
            window.openArchive(arc);
            QTRY_VERIFY(panel.IsArcFolder());
            QTRY_COMPARE(view.rowCount(), 2);
            QVERIFY(dynamic_cast<CAgentFolder *>(panel.GetFolder()));
        }
        panel.Set_AutoRefresh_Mode(false);
        panel.AttachExternalFileServices(&external);
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        QAbstractItemView *control = &view;
        if (mode == 0)
            control = window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                          ->findChild<QListView *>("iconList");
        QVERIFY(control);
        int row = -1;
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data().toString() == target)
                row = i;
        QVERIFY(row >= 0);
        control->setFocus();
        const auto index = control->model()->index(row, 0);
        control->scrollTo(index);
        QApplication::processEvents();
        QTest::mouseClick(control->viewport(), Qt::LeftButton, Qt::NoModifier,
                          control->visualRect(index).center());
        CMyComPtr<IFolderFolder> previous = panel.GetFolder();
        const UString prefix = panel.GetCurrentFolderPrefix();
        const auto selection = view.selectionModel()->selection();
        {
            CPanel::CDisableNotify guard(panel);
            activateWithKeyboard(window, control);
            QTest::mouseDClick(control->viewport(), Qt::LeftButton, Qt::ShiftModifier,
                               control->visualRect(index).center());
            QCOMPARE(panel.GetFolder(), (IFolderFolder *)previous);
            QCOMPARE(panel.GetCurrentFolderPrefix(), prefix);
            QVERIFY(external.paths.isEmpty());
        }
        bool shown = false;
        QTimer responder;
        responder.setSingleShot(true);
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            if (!dialog)
                return;
            const auto close = qScopeGuard([&] { dialog->reject(); });
            shown = true;
            auto *values = dialog->findChild<QTreeWidget *>("propertiesList");
            QVERIFY(values);
            QVERIFY(values->findItems(target, Qt::MatchExactly, 1).size() == 1);
            for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
                dialog->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*values, *dialog));
                QVERIFY(UiInspection::writeReport(*dialog, QString("activation-properties-%1-%2")
                                                               .arg(QTest::currentDataTag())
                                                               .arg(size.width())));
            }
        });
        responder.start(0);
        if (mouse)
            QTest::mouseDClick(control->viewport(), Qt::LeftButton, modifiers,
                               control->visualRect(index).center());
        else
            activateWithKeyboard(window, control, modifiers);
        QApplication::processEvents();
        responder.stop();
        QCOMPARE(shown, properties);
        if (properties || outside) {
            QCOMPARE(panel.GetFolder(), (IFolderFolder *)previous);
            QCOMPARE(panel.GetCurrentFolderPrefix(), prefix);
            QCOMPARE(view.selectionModel()->selection(), selection);
        } else {
            QVERIFY(panel.GetFolder() != (IFolderFolder *)previous);
            QVERIFY(ToQString(panel.GetCurrentFolderPrefix()).contains(target));
            QCOMPARE(view.rowCount(), 1);
        }
        QCOMPARE(external.paths.size(), outside ? 1 : 0);
        if (outside)
            QCOMPARE(QDir::cleanPath(external.paths[0]),
                     QDir(archive ? arc : files).filePath(target));
        QCOMPARE(peer.sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer.rowCount(), 2);
        QCOMPARE(peer.selectionModel()->selection(), peerSelection);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*control, window));
            QVERIFY(UiInspection::fullyVisible(peer, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("activation-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        QCOMPARE(read(arc), original);
        QCOMPARE(read(QDir(files).filePath("root.txt")), QByteArray("untouched"));
        QCOMPARE(read(QDir(files).filePath(target + "/child δ.txt")), QByteArray("untouched"));
    }
    void missingDiskFileCanBeOpenedAfterRestoration_data() {
        QTest::addColumn<bool>("mouse");
        QTest::newRow("keyboard") << false;
        QTest::newRow("double-click") << true;
    }
    void missingDiskFileCanBeOpenedAfterRestoration() {
        QFETCH(bool, mouse);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("документ 空.txt");
        QFile file(fixture.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ActivationExternalFiles external;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(fixture.path());
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto &view = list(window, 0);
        QTRY_VERIFY(view.isEnabled() && filesystemRow(window, name) >= 0);
        view.sourcePanel->Set_AutoRefresh_Mode(false);
        view.sourcePanel->AttachExternalFileServices(&external);
        const int row = filesystemRow(window, name);
        const auto index = view.model()->index(row, 0);
        const auto point = view.visualRect(index).center();
        view.setFocus();
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        const auto activate = [&] {
            if (mouse)
                QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            else
                activateWithKeyboard(window, &view);
        };
        QVERIFY(file.remove());
        QStringList errors;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
            }
        });
        answer.start(5);
        activate();
        answer.stop();
        QVERIFY(external.paths.isEmpty());
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.constFirst().normalized(QString::NormalizationForm_C).contains(name));
        QVERIFY(view.selectionModel()->isSelected(index));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        activate();
        QCOMPARE(external.paths.size(), 1);
        QCOMPARE(external.paths.constFirst().normalized(QString::NormalizationForm_C),
                 file.fileName());
    }
    void failedFolderRecovery_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("mouse");
        for (unsigned pane : {0u, 1u})
            for (bool ru : {false, true})
                for (int mode : {0, 3})
                    for (bool mouse : {false, true})
                        QTest::newRow(qPrintable(
                            QString("%1-%2-%3-%4").arg(pane).arg(ru).arg(mode).arg(mouse)))
                            << pane << ru << mode << mouse;
    }
    void failedFolderRecovery() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(int, mode);
        QFETCH(bool, mouse);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString target = fixture.filePath(QStringLiteral("target 文件"));
        QVERIFY(QDir().mkpath(target));
        const QString payload = fixture.filePath("root.txt");
        QFile file(payload);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("control"), qint64(7));
        file.close();
        ActivationExternalFiles external;
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        navigate(window, 1 - pane, fixture.path());
        navigate(window, pane, fixture.path());
        auto &view = list(window, pane);
        auto &panel = *view.sourcePanel;
        panel.AttachExternalFileServices(&external);
        CMyComPtr<IFolderFolder> previous = panel.GetFolder();
        CMyComPtr<IFolderFolder> peer = list(window, 1 - pane).sourcePanel->GetFolder();
        const UString prefix = panel.GetCurrentFolderPrefix();
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        QAbstractItemView *control = &view;
        if (mode == 0)
            control = window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                          ->findChild<QListView *>("iconList");
        QVERIFY(control);
        int row = 0;
        while (row < view.rowCount() &&
               view.model()->index(row, 0).data().toString() != QFileInfo(target).fileName())
            ++row;
        QVERIFY(row < view.rowCount());
        control->setFocus();
        const auto index = control->model()->index(row, 0);
        QTest::mouseClick(control->viewport(), Qt::LeftButton, Qt::NoModifier,
                          control->visualRect(index).center());
        const auto selection = view.selectionModel()->selection();
        QVERIFY(QDir().rmdir(target));
        bool error = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    error = !box->text().isEmpty();
                    box->accept();
                }
        });
        responder.start(0);
        const auto activate = [&] {
            if (mouse)
                QTest::mouseDClick(control->viewport(), Qt::LeftButton, Qt::NoModifier,
                                   control->visualRect(index).center());
            else
                activateWithKeyboard(window, control);
        };
        activate();
        responder.stop();
        QVERIFY(error);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)previous);
        QCOMPARE(panel.GetCurrentFolderPrefix(), prefix);
        QCOMPARE(view.selectionModel()->selection(), selection);
        QVERIFY(external.paths.isEmpty());
        QVERIFY(QDir().mkpath(target));
        activate();
        QVERIFY(panel.GetFolder() != (IFolderFolder *)previous);
        QCOMPARE(view.rowCount(), 0);
        QCOMPARE(list(window, 1 - pane).sourcePanel->GetFolder(), (IFolderFolder *)peer);
        QCOMPARE(list(window, 1 - pane).rowCount(), 2);
        QCOMPARE(read(payload), QByteArray("control"));
    }
};
QTEST_MAIN(PanelActivationTests)
#include "panel_activation_tests.moc"
