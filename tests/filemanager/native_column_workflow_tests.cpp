#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/schema_folder.h"
#include "filemanager/PropertyName.h"
#include "filemanager/support/property_projection_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QPersistentModelIndex>
#include <QStandardPaths>
#include <QTreeWidget>

class NativeColumnWorkflowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static FileTable &view(MainWindow &window, unsigned pane) {
        return *static_cast<FileTable *>(
            window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                ->findChild<FileTable *>("archiveTable"));
    }
    static void focusPath(MainWindow &window, unsigned pane) {
        window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
            ->findChild<QLineEdit *>("archivePath")
            ->setFocus();
    }
    static void navigate(MainWindow &window, unsigned pane, const QString &path) {
        focusPath(window, pane);
        auto *address = window.findChild<QWidget *>(QString("filePanel%1").arg(pane))
                            ->findChild<QLineEdit *>("archivePath");
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view(window, pane).rowCount(), 2);
        view(window, pane).sourcePanel->Set_AutoRefresh_Mode(false);
    }
    static void inspect(MainWindow &window, FileTable &list, FileTable &peer,
                        const QString &phase) {
        const auto &columns = list.sourcePanel->GetColumns();
        FOR_VECTOR(i, columns) {
            const int column = list.columnForCatalogProperty(i);
            QVERIFY(column >= 0);
            QCOMPARE(list.isColumnHidden(column), !columns[i].IsVisible);
            QCOMPARE(list.model()->headerData(column, Qt::Horizontal).toString(),
                     ToQString(columns[i].Name));
            for (int row = 0; row < list.rowCount(); ++row) {
                QCOMPARE(list.model()
                             ->index(row, column)
                             .data(Qt::TextAlignmentRole)
                             .value<Qt::Alignment>(),
                         GetColumnAlign(columns[i].ID, columns[i].Type));
                QString expected = list.sourcePanel->GetItemText(
                    list.model()->index(row, 0).data(Qt::UserRole + 6).toUInt(), columns[i]);
                // The retained Qt logical-zero display boundary composes a
                // flat prefix and NFC, while source item names stay raw.
                if (column == 0)
                    expected =
                        (list.model()->index(row, 0).data(Qt::UserRole + 5).toString() + expected)
                            .normalized(QString::NormalizationForm_C);
                QCOMPARE(list.model()->index(row, column).data().toString(), expected);
            }
        }
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(list, window));
            QVERIFY(UiInspection::fullyVisible(peer, window));
            QVERIFY(UiInspection::writeReport(window, QString("native-columns-%1-%2-%3")
                                                          .arg(QTest::currentDataTag(), phase)
                                                          .arg(size.width())));
        }
    }
    static void checkCrcProperties(CPanel &panel, UInt32 index, const QString &expected,
                                   const QString &tag) {
        CRecordVector<UInt32> indices;
        indices.Add(index);
        PropertyProjection properties(panel, indices);
        auto *values = properties.findChild<QTreeWidget *>("propertiesList");
        QVERIFY(values);
        bool crcFound = false;
        for (int i = 0; i < values->topLevelItemCount(); ++i)
            if (values->topLevelItem(i)->text(0) ==
                    ToQString(GetNameOfProperty(kpidCRC, nullptr)) &&
                values->topLevelItem(i)->text(1) == expected)
                crcFound = true;
        QVERIFY(crcFound);
        properties.show();
        for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
            properties.resize(size);
            QApplication::processEvents();
            auto *buttons = properties.findChild<QDialogButtonBox *>("listViewButtons");
            QVERIFY(buttons);
            QVERIFY(UiInspection::fullyVisible(*values, properties));
            QVERIFY(UiInspection::fullyVisible(*buttons, properties));
            QVERIFY(UiInspection::separate(*values, *buttons, properties));
            QVERIFY(UiInspection::writeReport(properties, QString("directory-crc-%1-%2-%3")
                                                              .arg(QTest::currentDataTag(), tag)
                                                              .arg(size.width())));
        }
        QTest::keyClick(&properties, Qt::Key_Escape);
        QVERIFY(!properties.isVisible());
        QCOMPARE(properties.result(), int(QDialog::Rejected));
    }
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; real column workflow unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        version_ = backend.codecCatalog()->version();
#endif
    }
    void providerColumnsAndSettings_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<QString>("format");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (const QString format : {"zip", "7z"})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(russian).arg(format)))
                        << pane << russian << format;
    }
    void providerColumnsAndSettings() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(QString, format);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString files = fixture.filePath("files");
        QVERIFY(QDir().mkpath(files));
        const QStringList names{QStringLiteral("first 文件.txt"), QStringLiteral("second δ.txt")};
        for (const QString &name : names) {
            QFile file(QDir(files).filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("untouched"), qint64(9));
        }
        const QString archive = fixture.filePath("archive with spaces." + format);
        QProcess creator;
        creator.setWorkingDirectory(files);
        creator.start(engine_, QStringList{"a", "-t" + format, "--", archive} + names);
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray archiveBytes = read(archive);
        QVERIFY(!archiveBytes.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        navigate(window, 1 - pane, files);
        navigate(window, pane, files);
        auto &list = view(window, pane);
        auto &panel = *list.sourcePanel;
        auto &peer = view(window, 1 - pane);
        peer.sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> peerProvider = peer.sourcePanel->GetFolder();
        const int peerRows = peer.rowCount();
        QVERIFY(panel.IsFSFolder());
        const int time = panel.GetColumns().FindItem_for_PropID(kpidATime);
        QVERIFY(time >= 0);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            menu->actions()[time]->trigger();
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(list.horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        list.horizontalHeader()->resizeSection(0, 187);
        list.horizontalHeader()->moveSection(list.horizontalHeader()->visualIndex(0),
                                             list.columnCount() - 1);
        inspect(window, list, peer, "filesystem");
        focusPath(window, pane);
        window.openArchive(archive);
        QTRY_VERIFY(panel.IsArcFolder());
        QTRY_COMPARE(list.rowCount(), 2);
        QVERIFY(dynamic_cast<CAgentFolder *>(panel.GetFolder()));
        panel.Set_AutoRefresh_Mode(false);
        QVERIFY(panel.GetColumns().FindItem_for_PropID(kpidPackSize) >= 0);
        list.horizontalHeader()->resizeSection(0, 213);
        inspect(window, list, peer, "archive");
        QVERIFY(WindowActivation::waitForActive(window));
        focusPath(window, pane);
        action(window, "openRootAction")->trigger();
        auto *spec = new SchemaFolder;
        CMyComPtr<IFolderFolder> provider = spec;
        spec->rawEnabled = false;
        spec->properties = {
            {kpidName, VT_BSTR}, {kpidCRC, VT_UI4}, {10001, VT_EMPTY, S_OK, UString(L"column α")}};
        panel.SetNewFolder(provider);
        panel.LoadFullPath();
        panel.OnReload();
        const int crc = list.columnForCatalogProperty(1);
        QVERIFY(crc >= 0);
        list.horizontalHeader()->resizeSection(crc, 173);
        panel.SelectAll(true);
        focusPath(window, 1 - pane);
        QWidget *const focused = window.focusWidget();
        const CPropColumns unchanged = panel.GetVisibleColumns();
        CSelectedState beforeInitialization;
        panel.SaveSelectedState(beforeInitialization);
        const QPersistentModelIndex retained = list.model()->index(0, crc);
        QCOMPARE(panel.InitColumns(), S_OK);
        QVERIFY(panel.GetVisibleColumns().IsEqualTo(unchanged));
        QVERIFY(retained.isValid());
        QVERIFY(!panel.IsItemMarked(0));
        QCOMPARE(list.selectionModel()->selectedRows().size(), 2);
        QCOMPARE(panel.RefreshListCtrl(beforeInitialization), S_OK);
        panel.OnReload();
        inspect(window, list, peer, "schema-original");
        spec->properties[1].type = VT_BSTR;
        spec->crcText = L"typed 文件";
        panel.OnReload();
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
        QVERIFY(!panel.GetVisibleColumns().IsEqualTo(unchanged));
        QCOMPARE(panel.GetVisibleColumns()[1].Width, UInt32(173));
        QCOMPARE(panel.GetVisibleColumns()[1].Type, VARTYPE(VT_BSTR));
        QCOMPARE(list.model()->index(0, list.columnForCatalogProperty(1)).data().toString(),
                 QStringLiteral("typed 文件"));
        QCOMPARE(list.model()
                     ->index(0, list.columnForCatalogProperty(1))
                     .data(Qt::TextAlignmentRole)
                     .value<Qt::Alignment>(),
                 Qt::AlignLeft | Qt::AlignVCenter);
        QCOMPARE(
            list.model()->headerData(list.columnForCatalogProperty(2), Qt::Horizontal).toString(),
            QStringLiteral("column α"));
        inspect(window, list, peer, "schema-type");
        const CPropColumns beforeName = panel.GetVisibleColumns();
        spec->properties[2].name = L"column 文件";
        panel.OnReload();
        QVERIFY(!panel.GetVisibleColumns().IsEqualTo(beforeName));
        QCOMPARE(panel.GetVisibleColumns()[1].Width, UInt32(173));
        QCOMPARE(ToQString(panel.GetVisibleColumns()[2].Name), QStringLiteral("column 文件"));
        QCOMPARE(
            list.model()->headerData(list.columnForCatalogProperty(2), Qt::Horizontal).toString(),
            QStringLiteral("column 文件"));
        QCOMPARE(list.selectionModel()->selectedRows().size(), 2);
        QCOMPARE(window.focusWidget(), focused);
        inspect(window, list, peer, "schema-name");
        navigate(window, pane, files);
        QVERIFY(panel.IsFSFolder());
        QCOMPARE(list.columnWidth(0), 187);
        const int restoredTime = panel.GetColumns().FindItem_for_PropID(kpidATime);
        QVERIFY(restoredTime >= 0 && panel.GetColumns()[restoredTime].IsVisible);
        QCOMPARE(panel.GetVisibleColumns()[0].Order, int(panel.GetVisibleColumns().Size() - 1));
        inspect(window, list, peer, "filesystem-restored");
        focusPath(window, pane);
        window.openArchive(archive);
        QTRY_VERIFY(panel.IsArcFolder());
        QTRY_COMPARE(list.rowCount(), 2);
        QCOMPARE(list.columnWidth(0), 213);
        inspect(window, list, peer, "archive-restored");
        QCOMPARE(peer.sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
        QCOMPARE(peer.rowCount(), peerRows);
        QCOMPARE(read(archive), archiveBytes);
        for (const QString &name : names)
            QCOMPARE(read(QDir(files).filePath(name)), QByteArray("untouched"));
    }
    void directoryCrcProjection_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<QString>("format");
        for (unsigned pane : {0u, 1u})
            for (bool russian : {false, true})
                for (const QString format : {"zip", "7z"})
                    QTest::newRow(
                        qPrintable(QString("crc-%1-%2-%3").arg(pane).arg(russian).arg(format)))
                        << pane << russian << format;
    }
    void directoryCrcProjection() {
        QFETCH(unsigned, pane);
        QFETCH(bool, russian);
        QFETCH(QString, format);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString high = QStringLiteral("a high 空"), low = QStringLiteral("z low й");
        QVERIFY(QDir(fixture.path()).mkpath(high));
        QVERIFY(QDir(fixture.path()).mkpath(low));
        const QMap<QString, QByteArray> inputs{{high + "/one.txt", "hello"},
                                               {high + "/two.txt", "world"},
                                               {low + "/one.txt", "hello"}};
        for (auto i = inputs.cbegin(); i != inputs.cend(); ++i) {
            QFile file(fixture.filePath(i.key()));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(i.value()), qint64(i.value().size()));
        }
        const QString archive = fixture.filePath(QStringLiteral("CRC архив 空.") + format);
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, QStringList{"a", "-t" + format, "--", archive} + inputs.keys());
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray archiveBytes = read(archive);
        QVERIFY(!archiveBytes.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        focusPath(window, pane);
        window.openArchive(archive);
        auto &list = view(window, pane), &peer = view(window, 1 - pane);
        QTRY_VERIFY(list.sourcePanel->IsArcFolder());
        QTRY_COMPARE(list.rowCount(), 2);
        auto &panel = *list.sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        peer.sourcePanel->Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> provider = panel.GetFolder();
        CMyComPtr<IFolderFolder> peerProvider = peer.sourcePanel->GetFolder();
        const int peerRows = peer.rowCount();
        {
            focusPath(window, pane);
            QTRY_COMPARE(list.rowCount(), 2);
            const int crcIndex = panel.GetColumns().FindItem_for_PropID(kpidCRC);
            QVERIFY(crcIndex >= 0);
            const int column = list.columnForCatalogProperty(crcIndex);
            QVERIFY(column >= 0);
            panel.SelectAll(true);
            const int selected = list.selectionModel()->selectedRows().size();
            for (const auto &expected :
                 QList<QPair<QString, QString>>{{high, "7087B7C9"}, {low, "3610A686"}}) {
                int row = -1;
                for (int i = 0; i < list.rowCount(); ++i)
                    if (list.model()
                            ->index(i, 0)
                            .data(Qt::UserRole)
                            .toString()
                            .normalized(QString::NormalizationForm_C) ==
                        expected.first.normalized(QString::NormalizationForm_C))
                        row = i;
                QVERIFY(row >= 0);
                const UInt32 index = list.model()->index(row, 0).data(Qt::UserRole + 6).toUInt();
                QCOMPARE(list.model()->index(row, column).data().toString(), expected.second);
                QCOMPARE(panel.GetItemText(index, panel.GetColumns()[crcIndex]), expected.second);
                checkCrcProperties(panel, index, expected.second, expected.first);
                QVERIFY(!QTest::currentTestFailed());
            }
            panel.SortItemsWithPropID(kpidCRC);
            if (!panel._ascending)
                panel.SortItemsWithPropID(kpidCRC);
            panel.OnReload();
            QTRY_COMPARE(list.rowCount(), 2);
            QCOMPARE(list.model()
                         ->index(0, 0)
                         .data(Qt::UserRole)
                         .toString()
                         .normalized(QString::NormalizationForm_C),
                     low);
            QCOMPARE(list.selectionModel()->selectedRows().size(), selected);
            inspect(window, list, peer, "crc-normal");
            QCOMPARE(panel.GetFolder(), (IFolderFolder *)provider);
            QCOMPARE(peer.sourcePanel->GetFolder(), (IFolderFolder *)peerProvider);
            QCOMPARE(peer.rowCount(), peerRows);
        }
        QCOMPARE(read(archive), archiveBytes);
        for (auto i = inputs.cbegin(); i != inputs.cend(); ++i)
            QCOMPARE(read(fixture.filePath(i.key())), i.value());
    }
};
QTEST_MAIN(NativeColumnWorkflowTests)
#include "native_column_workflow_tests.moc"
