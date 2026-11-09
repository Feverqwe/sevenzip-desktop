#include "Windows/PropVariant.h"
#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "support/progress_dialog.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"
#include <QLabel>

namespace {
class CancelDuringWalk final : public IProgress, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_1(IProgress)
    bool entered_ = false;

  public:
    Z7_COM7F_IMF(SetTotal(UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *)) override {
        if (entered_)
            return E_ABORT;
        entered_ = true;
        return S_OK;
    }
};

bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class FolderSizeViewTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cancellationInsideAFlatDirectoryLeavesTheSizeUnset() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString folder = fixture.filePath("large");
        QVERIFY(QDir().mkdir(folder));
        for (int index = 0; index < 320; ++index)
            QVERIFY(writeFile(QDir(folder).filePath(QString::number(index)), "123"));
        MainWindow window;
        window.show();
        window.openPath(fixture.path());
        QTRY_VERIFY(filesystemRow(window, "large") >= 0);
        auto *view = table(window);
        auto *provider = view->sourcePanel->GetFolder();
        CMyComPtr<IFolderCalcItemFullSize> sizes;
        QCOMPARE(provider->QueryInterface(IID_IFolderCalcItemFullSize,
                                          reinterpret_cast<void **>(&sizes)),
                 S_OK);
        const int row = filesystemRow(window, "large");
        const UInt32 index =
            view->model()->index(row, 0).data(QtPanelList::SourceIndexRole).toUInt();
        CMyComPtr<IProgress> cancel = new CancelDuringWalk;
        QCOMPARE(sizes->CalcItemFullSize(index, cancel), E_ABORT);
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_EMPTY));
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        action(window, "calculateDirectorySizeAction")->trigger();
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(960));
        cancel = new CancelDuringWalk;
        QCOMPARE(sizes->CalcItemFullSize(index, cancel), E_ABORT);
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(960));
    }

    void unavailableFolderDoesNotPublishAZeroSize() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString parked = fixture.filePath("parked");
        for (const QString name : {"folder", "other"}) {
            QVERIFY(QDir().mkdir(fixture.filePath(name)));
            QVERIFY(writeFile(fixture.filePath(name + "/file.txt"), "payload"));
        }
        MainWindow window;
        window.show();
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_COMPARE(view->rowCount(), 2);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *provider = view->sourcePanel->GetFolder();
        CMyComPtr<IFolderCalcItemFullSize> sizes;
        QCOMPARE(provider->QueryInterface(IID_IFolderCalcItemFullSize,
                                          reinterpret_cast<void **>(&sizes)),
                 S_OK);
        const auto sourceIndex = [&](int row) {
            return view->model()->index(row, 0).data(QtPanelList::SourceIndexRole).toUInt();
        };
        const int row = sourceIndex(0) < sourceIndex(1) ? 0 : 1;
        const UInt32 index = sourceIndex(row);
        const UInt32 otherIndex = sourceIndex(1 - row);
        const QString name = view->model()->index(row, 0).data().toString();
        const QString folder = fixture.filePath(name);
        QCOMPARE(sizes->CalcItemFullSize(index, nullptr), S_OK);
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(7));
        QVERIFY(QDir().rename(folder, parked));
        view->selectAll();
        QString report;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *result = TestProgressDialog::ResultWindow(widget);
                    result && result->isVisible()) {
                    report = TestProgressDialog::ReportText(*result);
                    TestProgressDialog::CloseResult(*result);
                }
            }
        });
        responder.start(1);
        action(window, "calculateDirectorySizeAction")->trigger();
        responder.stop();
        QVERIFY(report.contains(name));
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(provider->GetProperty(otherIndex, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(7));
        QVERIFY(QDir().rename(parked, folder));
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        action(window, "calculateDirectorySizeAction")->trigger();
        QCOMPARE(provider->GetProperty(index, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(7));
    }

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
        const QString folder = QStringLiteral("aaa папка й space");
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
        const int sizeColumn = view->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        view->sortItems(sizeColumn, Qt::AscendingOrder);
        QVERIFY(filesystemRow(window, folder) < filesystemRow(window, "empty"));
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
        auto *selectedSize = window.findChild<QLabel *>("selectedSizeStatus");
        QVERIFY(selectedSize);
        QTRY_VERIFY(selectedSize->text().isEmpty());
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
        QVERIFY(filesystemRow(window, "empty") < filesystemRow(window, folder));
        QCOMPARE(view->model()->index(filesystemRow(window, folder), sizeColumn).data().toString(),
                 QtRegionalFormat::bytes(8));
        QCOMPARE(view->model()->index(filesystemRow(window, "empty"), sizeColumn).data().toString(),
                 QtRegionalFormat::bytes(0));
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                 QString("focused.txt"));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 2);
        QCOMPARE(selectedSize->text(), QtRegionalFormat::bytes(8));
        view->selectionModel()->select(
            view->model()->index(filesystemRow(window, "focused.txt"), 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        QTRY_COMPARE(selectedSize->text(), QtRegionalFormat::bytes(9));
        QVERIFY(UiInspection::fullyVisible(*view, window));
        QVERIFY(UiInspection::writeReport(window,
                                          QString("folder-size-%1").arg(QTest::currentDataTag())));
#ifndef Q_OS_WIN
        if (useMenu) {
            action(window, "selectedPropertiesAction")->trigger();
            QPointer<QtPropertiesWindow> selection = window.findChild<QtPropertiesWindow *>();
            QVERIFY(selection);
            QCOMPARE(selection->findChild<QLabel *>("propertiesSize")->text(),
                     QtRegionalFormat::bytes(9));
            selection->close();
            QTRY_VERIFY(!selection);
            for (const auto &[name, bytes] :
                 {std::pair{folder, quint64(8)}, std::pair{QString("empty"), quint64(0)}}) {
                view->setCurrentCell(filesystemRow(window, name), 0);
                view->selectRow(filesystemRow(window, name));
                action(window, "selectedPropertiesAction")->trigger();
                QPointer<QtPropertiesWindow> properties = window.findChild<QtPropertiesWindow *>();
                QVERIFY(properties);
                QCOMPARE(properties->findChild<QLabel *>("propertiesSize")->text(),
                         QtRegionalFormat::bytes(bytes));
                properties->close();
                QTRY_VERIFY(!properties);
            }
        }
#endif
        QCOMPARE(backend.extractCalls, 0);
    }
};
QTEST_MAIN(FolderSizeViewTests)
#include "folder_size_view_tests.moc"
