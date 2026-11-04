#include "filemanager/support/window_state_support.h"
#include "filemanager/window/qtwindowworkspace.h"
#include "platform/support/external_editor_fixture.h"
#include "support/widget_dialogs.h"
#include <QClipboard>
#include <QEventLoop>
#include <QScopeGuard>
#include <QTabBar>

using namespace ExternalEditorFixture;

class WorkspaceArchiveTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture widgetDialogs_;
    static QtWindowWorkspace *workspace(MainWindow &window) {
        for (auto *object : window.children())
            if (auto *service = dynamic_cast<QtWindowWorkspace *>(object))
                return service;
        return nullptr;
    }
    static FileTable *active(MainWindow &window) {
        for (auto *view : window.findChildren<FileTable *>())
            if (view->isVisible())
                return view;
        return nullptr;
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void movedArchiveCanExportAfterOriginalWindowAndBackendAreDestroyed() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("данные file.txt");
        const QByteArray bytes("archive clipboard\0bytes", 23);
        QVERIFY(write(fixture.filePath(name), bytes));
        const QString archive = fixture.filePath("moving archive.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString destination = fixture.filePath("Export target");
        QVERIFY(QDir().mkpath(destination));
        auto originalBackend = std::make_unique<ArchiveBackend>();
        auto original = std::make_unique<MainWindow>(originalBackend.get());
        ArchiveBackend receiverBackend;
        MainWindow receiver(&receiverBackend);
        original->show();
        receiver.show();
        original->openArchive(archive);
        QTRY_COMPARE(active(*original)->rowCount(), 1);
        QTRY_VERIFY(active(*original)->isEnabled());
        active(*original)->selectRow(0);
        active(*original)->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        auto *tabs = original->findChild<QTabBar *>("panelTabs0");
        receiver.openPath(destination);
        QTRY_VERIFY(active(receiver)->isEnabled());
        const QString id = tabs->tabData(0).toString();
        bool moved = false;
        QTRY_VERIFY(moved ||
                    (moved = workspace(*original)->TransferTab(id, *workspace(receiver), 0)));
        original.reset();
        originalBackend.reset();
        QTRY_COMPARE(active(receiver)->rowCount(), 1);
        QTRY_VERIFY(action(receiver, "copyFilesAction")->isEnabled());
        action(receiver, "copyFilesAction")->trigger();
        QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
        receiver.openPath(destination);
        QTRY_VERIFY(action(receiver, "pasteFilesAction")->isEnabled());
        action(receiver, "pasteFilesAction")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(read(destination + '/' + name), bytes, 10000);
        QTRY_VERIFY(active(receiver)->isEnabled());
        QApplication::clipboard()->clear();
    }
    void externalEditReturnsToInactiveTabWithoutChangingActiveSelection() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("edited file.txt");
        QVERIFY(write(fixture.filePath(name), "original"));
        const QString archive = fixture.filePath("tab context.zip");
        QVERIFY(createArchive(fixture.path(), archive, name));
        const QString other = fixture.filePath("Other folder");
        QVERIFY(QDir().mkpath(other));
        QVERIFY(write(other + "/keep selected.txt", "peer"));
        const QString control = fixture.filePath("tab editor control");
        const auto stopEditor = qScopeGuard([&] { write(control + ".exit", "exit"); });
        configure(control);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        action(window, "editFileAction")->trigger();
        QTRY_VERIFY(QFileInfo(control + ".ready").size() > 0);
        const QString temporary = QString::fromUtf8(read(control + ".ready"));
        action(window, "newTabAction")->trigger();
        auto *tabs = window.findChild<QTabBar *>("panelTabs0");
        QTRY_COMPARE(tabs->count(), 2);
        const auto active = [&window]() -> FileTable * {
            for (auto *view : window.findChildren<FileTable *>())
                if (view->isVisible())
                    return view;
            return nullptr;
        };
        QTRY_VERIFY(active() && active()->isEnabled());
        window.openPath(other);
        auto *peer = active();
        int row = -1;
        for (int i = 0; i < peer->rowCount(); ++i)
            if (peer->model()->index(i, 0).data().toString() == "keep selected.txt")
                row = i;
        QVERIFY(row >= 0);
        peer->selectRow(row);
        peer->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        bool asked = false;
        QString errors;
        QTimer responder;
        widgetDialogs_.connectResponder(responder, &window, [&] {
            auto *box = qobject_cast<QMessageBox *>(widgetDialogs_.activeDialog());
            if (box && box->objectName() == "externalUpdatePrompt") {
                asked = true;
                box->button(QMessageBox::Yes)->click();
            } else if (box) {
                errors += box->text();
                box->accept();
            }
        });
        responder.start(1);
        QVERIFY(write(control, "change"));
        QVERIFY(write(control + ".exit", "exit"));
        QEventLoop delivery;
        QTimer finished;
        connect(&finished, &QTimer::timeout, &delivery, [&] {
            if (!QFileInfo::exists(temporary))
                delivery.quit();
        });
        finished.start(5);
        QTimer::singleShot(10000, &delivery, &QEventLoop::quit);
        delivery.exec();
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QVERIFY(asked);
        QTRY_VERIFY(!QFileInfo::exists(temporary));
        QCOMPARE(tabs->currentIndex(), 1);
        QCOMPARE(QDir::cleanPath(ToQString(active()->sourcePanel->GetCurrentFolderPrefix())),
                 other);
        QCOMPARE(active()->model()->index(active()->currentRow(), 0).data().toString(),
                 "keep selected.txt");
        QVERIFY(active()->selectionModel()->isRowSelected(active()->currentRow(), {}));
        QCOMPARE(contents(archive, name), modified);
        tabs->setCurrentIndex(0);
        QTRY_COMPARE(active()->model()->index(0, 2).data().toString(),
                     QString::number(modified.size()));
    }
};

int main(int argc, char **argv) {
    const int fixture = ExternalEditorFixture::run(argc, argv);
    if (fixture >= 0)
        return fixture;
    QApplication app(argc, argv);
    WorkspaceArchiveTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "workspace_archive_tests.moc"
