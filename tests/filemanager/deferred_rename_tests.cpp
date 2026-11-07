#include "filemanager/panels/Panel.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "platform/support/external_editor_fixture.h"
#include "support/panel_keys.h"
#include <QAbstractItemDelegate>
#include <array>

class DeferredRenameTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
  private slots:
    void archiveRenameKeepsItsOriginalPanel() {
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settingsPath);
        CFmSettings settings;
        settings.Load(settingsPath);
        settings.ShowDots = false;
        settings.Save(settingsPath);
        QTemporaryDir source, peer;
        QVERIFY(source.isValid() && peer.isValid());
        const QString name = QStringLiteral("original 空.txt");
        const QString renamed = QStringLiteral("renamed δ.txt");
        const std::array<QString, 2> folders{source.path(), peer.path()};
        std::array<QString, 2> archives;
        for (unsigned index : {0u, 1u}) {
            QVERIFY(
                write(QDir(folders[index]).filePath(name), index ? "peer bytes" : "source bytes"));
            archives[index] = QDir(folders[index]).filePath("archive.zip");
            QVERIFY(ExternalEditorFixture::createArchive(folders[index], archives[index], name));
        }
        ArchiveBackend backend;
        QSignalSpy catalogReady(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(catalogReady.size(), 1);
        QVERIFY2(catalogReady[0][0].toBool(), qPrintable(catalogReady[0][1].toString()));
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        for (unsigned index : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            views[index] = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            address->setText(archives[index]);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(views[index]->rowCount(), 1);
            QVERIFY(views[index]->sourcePanel->IsArcFolder());
            views[index]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        CMyComPtr<IFolderFolder> peerFolder = views[1]->sourcePanel->GetFolder();
        views[0]->setFocus();
        QTest::keyClick(views[0], Qt::Key_Home);
        views[0]->selectRow(0);
        views[0]->setCurrentCell(0, 0);
        QVERIFY(action(window, "renameAction")->isEnabled());
        action(window, "renameAction")->trigger();
        auto *editor = views[0]->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(renamed);
        views[0]->itemDelegate()->commitData(editor);
        views[0]->itemDelegate()->closeEditor(editor);
        views[1]->setFocus();
        QTRY_COMPARE(views[0]->model()->index(0, 0).data(Qt::UserRole + 3).toString(), renamed);
        QCOMPARE(views[1]->model()->index(0, 0).data(Qt::UserRole + 3).toString(), name);
        QCOMPARE(views[1]->sourcePanel->GetFolder(), peerFolder.Interface());
        QCOMPARE(ExternalEditorFixture::contents(archives[0], renamed), QByteArray("source bytes"));
        QCOMPARE(ExternalEditorFixture::contents(archives[1], name), QByteArray("peer bytes"));
    }

    void deferredRenameKeepsItsOwner_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<int>("transition");
        for (unsigned pane : {0u, 1u}) {
            QTest::newRow(qPrintable(QString("focus-%1").arg(pane))) << pane << 0;
            QTest::newRow(qPrintable(QString("reload-%1").arg(pane))) << pane << 2;
        }
    }
    void deferredRenameKeepsItsOwner() {
        QFETCH(unsigned, pane);
        QFETCH(int, transition);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CFmSettings settings;
        settings.Load(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        settings.ShowDots = false;
        settings.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir source, peer;
        QVERIFY(source.isValid() && peer.isValid());
        const QString name = QStringLiteral("original 空.txt");
        const QString renamed = QStringLiteral("renamed δ.txt");
        const QString replacement = QStringLiteral("replacement 文件.txt");
        QVERIFY(write(source.filePath(name), "source bytes"));
        QVERIFY(write(peer.filePath(name), "peer bytes"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        std::array<FileTable *, 2> views;
        for (unsigned index : {0u, 1u}) {
            auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            views[index] = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
            auto *address = ui->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            address->setText(index == pane ? source.path() : peer.path());
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_COMPARE(views[index]->rowCount(), 1);
            views[index]->sourcePanel->Set_AutoRefresh_Mode(false);
        }
        auto *view = views[pane];
        view->setFocus();
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        QVERIFY(action(window, "renameAction")->isEnabled());
        action(window, "renameAction")->trigger();
        auto *editor = view->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(renamed);
        // Deliver the editor's public commit, then change focus/location before
        // the deferred filesystem operation gets its next event-loop turn.
        view->itemDelegate()->commitData(editor);
        view->itemDelegate()->closeEditor(editor);
        if (transition == 1)
            window.openPath(peer.path());
        else if (transition == 2) {
            QVERIFY(QFile::rename(source.filePath(name), source.filePath(replacement)));
            view->sourcePanel->OnReload(false);
        } else
            views[1 - pane]->setFocus();
        bool delivered = false;
        QTimer::singleShot(0, &window, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        const bool applied = transition == 0;
        QCOMPARE(QFileInfo::exists(source.filePath(renamed)), applied);
        QCOMPARE(QFileInfo::exists(source.filePath(name)), transition == 1);
        QVERIFY(QFileInfo::exists(peer.filePath(name)));
        QVERIFY(!QFileInfo::exists(peer.filePath(renamed)));
        QFile original(source.filePath(applied ? renamed : transition == 1 ? name : replacement));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("source bytes"));
    }
};
QTEST_MAIN(DeferredRenameTests)
#include "deferred_rename_tests.moc"
