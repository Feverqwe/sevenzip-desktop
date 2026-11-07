#include "filemanager/support/window_state_support.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include <QInputMethodEvent>
#include <QStyledItemDelegate>

namespace {
class RefusingEditor final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &,
                          const QModelIndex &) const override {
        return nullptr;
    }
};
} // namespace

class RenameLifecycleTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() {
        const QString path = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(path);
        CFmSettings settings;
        settings.Load(path);
        settings.ShowDots = false;
        settings.Save(path);
    }
    void displayRefreshKeepsEditorDecision_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("cancel");
        for (unsigned pane : {0u, 1u})
            for (bool icons : {false, true})
                for (bool cancel : {false, true})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(icons).arg(cancel)))
                        << pane << icons << cancel;
    }
    void displayRefreshKeepsEditorDecision() {
        QFETCH(unsigned, pane);
        QFETCH(bool, icons);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("original 空.txt");
        const QString renamed = QStringLiteral("renamed δ.txt");
        QFile input(fixture.filePath(original));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("source bytes"), qint64(12));
        input.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = ui->findChild<FileTable *>("archiveTable");
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 1);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *active =
            icons ? static_cast<QAbstractItemView *>(ui->findChild<QListView *>("iconList"))
                  : static_cast<QAbstractItemView *>(view);
        active->setFocus();
        action(window, "renameAction")->trigger();
        QPointer<QLineEdit> editor = active->findChild<QLineEdit *>();
        QVERIFY(editor);
        // The real language-change path refreshes display roles. It must
        // neither confirm nor retire an editor that the user still owns.
        editor->selectAll();
        QInputMethodEvent inputMethod;
        inputMethod.setCommitString(renamed);
        QApplication::sendEvent(editor, &inputMethod);
        QCOMPARE(editor->text(), renamed);
        view->invalidateLanguageText();
        QVERIFY(editor);
        QCOMPARE(editor->text(), renamed);
        QTest::keyClick(editor, cancel ? Qt::Key_Escape : Qt::Key_Return);
        QTRY_VERIFY(!editor);
        if (!cancel)
            QTRY_VERIFY(QFileInfo::exists(fixture.filePath(renamed)));
        QCOMPARE(QFileInfo::exists(fixture.filePath(original)), cancel);
        QCOMPARE(QFileInfo::exists(fixture.filePath(renamed)), !cancel);
        QFile output(fixture.filePath(cancel ? original : renamed));
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("source bytes"));
    }
    void refusedEditorDoesNotBlockOpening() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString child = fixture.filePath("child directory");
        QVERIFY(QDir().mkpath(child));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 1);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        view->setFocus();
        auto *original = view->itemDelegate();
        view->setItemDelegate(new RefusingEditor(view));
        action(window, "renameAction")->trigger();
        QVERIFY(!view->findChild<QLineEdit *>());
        view->setItemDelegate(original);
        QCoreApplication::processEvents();
        const QPoint point = view->visualRect(view->model()->index(0, 0)).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTRY_COMPARE(QDir::cleanPath(window.findChild<QLineEdit *>("archivePath")->text()),
                     QFileInfo(child).canonicalFilePath());
        QVERIFY(QFileInfo(child).isDir());
    }
    void renameRevealsFileAtItsNewSortedPosition_data() {
        QTest::addColumn<bool>("icons");
        QTest::newRow("table") << false;
        QTest::newRow("icons") << true;
    }
    void renameRevealsFileAtItsNewSortedPosition() {
        QFETCH(bool, icons);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = fixture.filePath("a.txt");
        const QString renamed = fixture.filePath("z.txt");
        QFile input(original);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("payload"), qint64(7));
        input.close();
        for (int i = 0; i < 120; ++i) {
            QFile peer(fixture.filePath(QString("m%1.txt").arg(i)));
            QVERIFY(peer.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(640, 400);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 121);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *active = icons ? window.findChild<QAbstractItemView *>("iconList") : view;
        QVERIFY(active);
        active->setFocus();
        const auto first = active->model()->index(0, 0);
        active->setCurrentIndex(first);
        action(window, "renameAction")->trigger();
        auto *editor = active->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->text(), QString("a.txt"));
        editor->setText("z.txt");
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(active->currentIndex().data(QtPanelList::NameRole).toString(),
                     QString("z.txt"));
        QVERIFY(active->selectionModel()->isRowSelected(active->currentIndex().row(), {}));
        QTRY_VERIFY(
            active->visualRect(active->currentIndex()).intersects(active->viewport()->rect()));
        QTRY_VERIFY(active->hasFocus());
        QVERIFY(!QFileInfo::exists(original));
        QFile output(renamed);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), QByteArray("payload"));
    }

    void destroyingWindowCancelsQueuedRename() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = fixture.filePath(QStringLiteral("original 空.txt"));
        const QString renamed = fixture.filePath(QStringLiteral("renamed δ.txt"));
        QFile input(original);
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("source bytes"), qint64(12));
        input.close();
        ControlledBackend backend;
        auto window = std::make_unique<MainWindow>(&backend);
        window->show();
        QVERIFY(WindowActivation::waitForActive(*window));
        window->openPath(fixture.path());
        auto *view = table(*window);
        QTRY_COMPARE(view->rowCount(), 1);
        view->setCurrentCell(0, 0);
        view->selectRow(0);
        action(*window, "renameAction")->trigger();
        auto *editor = view->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(QFileInfo(renamed).fileName());
        view->itemDelegate()->commitData(editor);
        view->itemDelegate()->closeEditor(editor);
        window.reset();
        QCoreApplication::processEvents();
        QVERIFY(QFileInfo::exists(original));
        QVERIFY(!QFileInfo::exists(renamed));
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("source bytes"));
    }
};

QTEST_MAIN(RenameLifecycleTests)
#include "rename_lifecycle_tests.moc"
