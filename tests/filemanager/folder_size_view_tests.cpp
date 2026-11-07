#include "Windows/PropVariant.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class FolderSizeViewTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void calculatesOperatedFoldersWithoutReloading_data() {
        QTest::addColumn<bool>("compact");
        QTest::addColumn<bool>("useMenu");
        QTest::newRow("normal-f3") << false << false;
        QTest::newRow("compact-f3") << true << false;
        QTest::newRow("normal-menu") << false << true;
    }
    void calculatesOperatedFoldersWithoutReloading() {
        QFETCH(bool, compact);
        QFETCH(bool, useMenu);
        QTemporaryDir fixture;
        const QString folder = QStringLiteral("папка й space");
        QVERIFY(QDir().mkpath(fixture.filePath(folder + "/child")));
        QVERIFY(QDir().mkpath(fixture.filePath("empty")));
        QVERIFY(writeFile(fixture.filePath(folder + "/one.txt"), "123"));
        QVERIFY(writeFile(fixture.filePath(folder + "/child/two.txt"), "45678"));
        QVERIFY(writeFile(fixture.filePath("focused.txt"), "9"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(compact ? QSize(640, 400) : QSize(980, 620));
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 3);
        auto *view = static_cast<FileTable *>(table(window));
        auto *provider = view->sourcePanel->GetFolder();
        const auto property = [&](const QString &name, PROPID id) {
            NWindows::NCOM::CPropVariant value;
            const int row = filesystemRow(window, name);
            if (row >= 0)
                provider->GetProperty(view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt(),
                                      id, &value);
            return value;
        };
        QCOMPARE(property(folder, kpidSize).vt, VARTYPE(VT_EMPTY));
        const int filesColumn = view->columnForProperty(kpidNumSubFiles);
        QVERIFY(filesColumn >= 0);
        view->setColumnHidden(filesColumn, true);
        QCOMPARE(view->model()->index(filesystemRow(window, folder), filesColumn).data().toString(),
                 QString());
        view->selectRow(filesystemRow(window, folder));
        QVERIFY(action(window, "viewFileAction")->isEnabled());
        QVERIFY(!action(window, "editFileAction")->isEnabled());
        // Source operated selection can contain directories while focus is a file.
        view->selectionModel()->select(view->model()->index(filesystemRow(window, "empty"), 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        view->setCurrentCell(filesystemRow(window, "focused.txt"), 0,
                             QItemSelectionModel::NoUpdate);
        window.activateWindow();
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        if (useMenu) {
            auto *calculate = action(window, "calculateDirectorySizeAction");
            QVERIFY(calculate->isEnabled());
            calculate->trigger();
        } else {
            QTest::keyClick(view, Qt::Key_F3);
        }
        QCOMPARE(view->sourcePanel->GetFolder(), provider);
        QCOMPARE(property(folder, kpidSize).vt, VARTYPE(VT_UI8));
        QCOMPARE(property(folder, kpidSize).uhVal.QuadPart, UInt64(8));
        QCOMPARE(property(folder, kpidNumSubFiles).uhVal.QuadPart, UInt64(2));
        QCOMPARE(view->model()->index(filesystemRow(window, folder), filesColumn).data().toString(),
                 QString("2"));
        QCOMPARE(property(folder, kpidNumSubDirs).uhVal.QuadPart, UInt64(1));
        QCOMPARE(property("empty", kpidSize).vt, VARTYPE(VT_UI8));
        QCOMPARE(property("empty", kpidSize).uhVal.QuadPart, UInt64(0));
        const int sizeColumn = view->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        QCOMPARE(view->model()->index(filesystemRow(window, folder), sizeColumn).data().toString(),
                 QtRegionalFormat::bytes(8));
        QCOMPARE(view->model()->index(filesystemRow(window, "empty"), sizeColumn).data().toString(),
                 QtRegionalFormat::bytes(0));
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                 QString("focused.txt"));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        QVERIFY(UiInspection::fullyVisible(*view, window));
        QVERIFY(UiInspection::writeReport(window,
                                          QString("folder-size-%1").arg(QTest::currentDataTag())));
        QCOMPARE(backend.extractCalls, 0);
    }
};
QTEST_MAIN(FolderSizeViewTests)
#include "folder_size_view_tests.moc"
