#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/PanelSort.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "ui/support/layout_inspection.h"

#include <QAbstractItemModelTester>
#include <QHeaderView>

struct CurrentDirectoryGuard {
    const QString previous = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(previous); }
};

class RawPropertyFolder final : public IFolderFolder,
                                public IArchiveGetRawProps,
                                public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IFolderFolder)
    Z7_COM_QI_ENTRY(IArchiveGetRawProps)
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
  public:
    unsigned rawRequests = 0;
    Z7_IFACE_COM7_IMP(IFolderFolder)
    Z7_IFACE_COM7_IMP(IArchiveGetRawProps)
};

Z7_COM7F_IMF(RawPropertyFolder::LoadItems()) { return S_OK; }
Z7_COM7F_IMF(RawPropertyFolder::GetNumberOfItems(UInt32 *count)) {
    *count = 2;
    return S_OK;
}
Z7_COM7F_IMF(RawPropertyFolder::GetNumberOfProperties(UInt32 *count)) {
    *count = 1;
    return S_OK;
}
Z7_COM7F_IMF(RawPropertyFolder::GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id,
                                                VARTYPE *type)) {
    if (index != 0)
        return E_INVALIDARG;
    *name = nullptr;
    *id = kpidName;
    *type = VT_BSTR;
    return S_OK;
}
Z7_COM7F_IMF(RawPropertyFolder::GetProperty(UInt32 index, PROPID id, PROPVARIANT *value)) {
    if (index >= 2)
        return E_INVALIDARG;
    NWindows::NCOM::CPropVariant prop;
    if (id == kpidName)
        prop = index == 0 ? L"z.txt" : L"a.txt";
    else if (id == kpidIsDir)
        prop = false;
    return prop.Detach(value);
}
Z7_COM7F_IMF(RawPropertyFolder::GetFolderProperty(PROPID id, PROPVARIANT *value)) {
    NWindows::NCOM::CPropVariant prop;
    if (id == kpidType)
        prop = L"7-Zip.raw";
    return prop.Detach(value);
}
Z7_COM7F_IMF(RawPropertyFolder::BindToFolder(UInt32, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(RawPropertyFolder::BindToFolder(const wchar_t *, IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(RawPropertyFolder::BindToParentFolder(IFolderFolder **folder)) {
    *folder = nullptr;
    return E_NOTIMPL;
}
Z7_COM7F_IMF(RawPropertyFolder::GetParent(UInt32, UInt32 *, UInt32 *)) { return E_FAIL; }
Z7_COM7F_IMF(RawPropertyFolder::GetNumRawProps(UInt32 *count)) {
    *count = 1;
    return S_OK;
}
Z7_COM7F_IMF(RawPropertyFolder::GetRawPropInfo(UInt32 index, BSTR *name, PROPID *id)) {
    if (index != 0)
        return E_INVALIDARG;
    *name = nullptr;
    *id = kpidCRC;
    return S_OK;
}
Z7_COM7F_IMF(RawPropertyFolder::GetRawProp(UInt32 index, PROPID id, const void **data, UInt32 *size,
                                           UInt32 *type)) {
    ++rawRequests;
    if (index >= 2 || id != kpidCRC)
        return E_INVALIDARG;
    static const Byte crc[] = {0xde, 0xad, 0xbe, 0xef};
    *data = index == 0 ? nullptr : crc;
    *size = index == 0 ? 0 : UInt32(sizeof(crc));
    *type = NPropDataType::kRaw;
    return S_OK;
}

class PanelColumnsTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.remove("Columns");
        settings.remove("view/timestampLevel");
        settings.remove("view/timestampUTC");
    }

    void rawCatalogTextAndSortUseSourceInterface() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CMyComPtr<IFolderFolder> folder = new RawPropertyFolder;
        CPanel panel(folder);
        panel.AttachFolderManager(nullptr, temp.filePath("settings.ini"));
        QCOMPARE(panel.InitColumns(), S_OK);
        const int rawIndex = panel.GetColumns().FindItem_for_PropID(kpidCRC);
        QVERIFY(rawIndex >= 0);
        const CPropColumn &raw = panel.GetColumns()[rawIndex];
        QVERIFY(raw.IsRawProp);
        QCOMPARE(panel.GetItemText(1, raw), QString("DEADBEEF"));
        QCOMPARE(panel.GetItemText(0, raw), QString());
        QVERIFY(panel.CompareItemIndices(0, 1, kpidCRC, true) < 0);
        QVERIFY(panel.CompareItemIndices(0, 1, kpidName) > 0);
    }

    void modelPreservesSelectionAndPersistentIndexes() {
        QTemporaryDir temp;
        CMyComPtr<IFolderFolder> folder = new RawPropertyFolder;
        CPanel panel(folder);
        panel.AttachFolderManager(nullptr, temp.filePath("settings.ini"));
        QCOMPARE(panel.LoadListItems(), S_OK);
        FileTable view(1);
        view.sourcePanel = &panel;
        view.textFolder = folder;
        view.setColumnProperties({kpidName});
        auto *model = view.panelModel();
        QAbstractItemModelTester tester(model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        for (quint32 i : {0u, 1u}) {
            FilePanelModel::Row row;
            row.sourceIndex = i;
            row.name = ToQString(panel.GetItemName(i));
            model->append(std::move(row));
        }
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        panel.AttachSelectionView(&view);
        view.selectRow(0);
        view.setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        QPersistentModelIndex selected = model->index(0, 0);
        view.sortItems(0, Qt::AscendingOrder);
        QCOMPARE(selected.row(), 1);
        QCOMPARE(selected.data(Qt::UserRole + 6).toUInt(), 0u);
        QVERIFY(view.selectionModel()->isSelected(selected));
        QCOMPARE(view.currentIndex(), selected);
        model->setColumns({"Name", "Extra"});
        QCOMPARE(selected.row(), 1);
        QVERIFY(view.selectionModel()->isSelected(selected));
        QVERIFY(model->setData(model->index(selected.row(), 1), true, QtPanelList::MarkedRole));
        QVERIFY(selected.data(Qt::BackgroundRole).isValid());
        model->removeRow(0);
        QCOMPARE(selected.row(), 0);
        QCOMPARE(selected.data(Qt::UserRole + 6).toUInt(), 0u);
        model->removeRow(0);
        QVERIFY(!selected.isValid());
    }

    void tableRequestsSourceTextOnlyForDisplay() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto *provider = new RawPropertyFolder;
        CMyComPtr<IFolderFolder> folder = provider;
        CPanel panel(folder);
        panel.AttachFolderManager(nullptr, temp.filePath("settings.ini"));
        QCOMPARE(panel.InitColumns(), S_OK);
        FileTable list(8);
        list.sourcePanel = &panel;
        list.textFolder = folder;
        QVector<PROPID> properties(8, kpidNoProperty);
        properties[7] = kpidCRC;
        list.setColumnProperties(properties);
        for (int row = 0; row < 2; ++row) {
            FilePanelModel::Row item;
            item.sourceIndex = row;
            list.panelModel()->append(std::move(item));
        }
        QCOMPARE(provider->rawRequests, 0u);
        QCOMPARE(list.model()->index(1, 7).data(Qt::UserRole + 6).toUInt(), 1u);
        QCOMPARE(provider->rawRequests, 0u);
        QCOMPARE(list.model()->index(1, 7).data().toString(), QString("DEADBEEF"));
        QCOMPARE(provider->rawRequests, 1u);
        QCOMPARE(list.model()->index(1, 7).data().toString(), QString("DEADBEEF"));
        QCOMPARE(provider->rawRequests, 1u);
        QCOMPARE(list.model()->index(0, 7).data().toString(), QString());
        QCOMPARE(provider->rawRequests, 2u);
        {
            CPanel::CDisableNotify guard(panel);
            SetPanelData(list.model()->index(1, 7), QVariant(), Qt::DisplayRole);
            QCOMPARE(list.model()->index(1, 7).data().toString(), QString());
            QCOMPARE(provider->rawRequests, 2u);
        }
        QCOMPARE(list.model()->index(1, 7).data().toString(), QString("DEADBEEF"));
        QCOMPARE(provider->rawRequests, 3u);
        list.clearRows();
        QCOMPARE(provider->rawRequests, 3u);
    }

    void sourceCatalogAndHeaderSettingsPersist() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QFile file(temp.filePath(QStringLiteral("имя с пробелом.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("1234567890");
        file.close();

        int accessTimeColumn = -1;
        int createdTimeColumn = -1;
        QString accessTimeLabel;
        {
            MainWindow window;
            window.show();
            auto *files = static_cast<FileTable *>(table(window));
            auto *header = files->horizontalHeader();
            accessTimeColumn = files->columnForProperty(kpidATime);
            createdTimeColumn = files->columnForProperty(kpidCTime);
            QVERIFY(accessTimeColumn >= 0);
            QVERIFY(createdTimeColumn >= 0);
            QVERIFY(files->isColumnHidden(accessTimeColumn));
            QVERIFY(!files->isColumnHidden(createdTimeColumn));
            accessTimeLabel =
                files->model()->headerData(accessTimeColumn, Qt::Horizontal).toString();
            QTimer::singleShot(0, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (QAction *option : menu->actions())
                    if (option->text() == accessTimeLabel)
                        option->trigger();
                menu->close();
            });
            QVERIFY(QMetaObject::invokeMethod(header, "customContextMenuRequested",
                                              Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
            QVERIFY(!files->isColumnHidden(accessTimeColumn));
            bool foundFile = false;
            for (int row = 0; row < files->rowCount(); ++row) {
                if (files->model()->index(row, 0).data().toString() ==
                    QStringLiteral("имя с пробелом.txt")) {
                    foundFile = true;
                    QVERIFY(
                        !files->model()->index(row, accessTimeColumn).data().toString().isEmpty());
                }
            }
            QVERIFY(foundFile);
            header->resizeSection(accessTimeColumn, 183);
            header->moveSection(header->visualIndex(accessTimeColumn), 1);
            files->sortItems(createdTimeColumn, Qt::DescendingOrder);
            QCOMPARE(header->sortIndicatorSection(), createdTimeColumn);
        }
        {
            MainWindow reopened;
            reopened.show();
            auto *files = static_cast<FileTable *>(table(reopened));
            auto *header = files->horizontalHeader();
            QCOMPARE(files->columnForProperty(kpidATime), accessTimeColumn);
            QCOMPARE(files->columnForProperty(kpidCTime), createdTimeColumn);
            QVERIFY(!files->isColumnHidden(accessTimeColumn));
            QCOMPARE(header->sectionSize(accessTimeColumn), 183);
            QCOMPARE(header->visualIndex(accessTimeColumn), 1);
            QCOMPARE(header->sortIndicatorSection(), createdTimeColumn);
            QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                reopened.resize(size);
                for (int mode = 0; mode < 4; ++mode) {
                    auto *modeAction =
                        reopened.findChild<QAction *>(QStringLiteral("viewMode%1Action").arg(mode));
                    QVERIFY(modeAction);
                    modeAction->trigger();
                    QCoreApplication::processEvents();
                    QVERIFY(UiInspection::fullyVisible(*reopened.centralWidget(), reopened));
                    QVERIFY(UiInspection::writeReport(
                        reopened,
                        QStringLiteral("source-columns-%1-%2").arg(size.width()).arg(mode)));
                }
            }
        }
    }

    void folderTypesKeepIndependentLayouts() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        QFile file(temp.filePath("local.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("disk");
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *files = static_cast<FileTable *>(table(window));
        auto *header = files->horizontalHeader();
        const int diskSize = files->columnForProperty(kpidSize);
        QVERIFY(!files->isColumnHidden(diskSize));
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (QAction *option : menu->actions())
                if (option->text() ==
                    files->model()->headerData(diskSize, Qt::Horizontal).toString())
                    option->trigger();
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(header, "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        QVERIFY(files->isColumnHidden(diskSize));

        openListedArchive(window, backend, temp.filePath("sample.7z"),
                          {{QStringLiteral("архив/файл с пробелом.txt"), 45, 18}});
        const int packedSize = files->columnForProperty(kpidPackSize);
        QVERIFY(packedSize >= 0);
        QVERIFY(!files->isColumnHidden(packedSize));
        QVERIFY(files->columnForProperty(kpidNumSubFiles) >= 0);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QVERIFY(files->isColumnHidden(diskSize));
    }

    void sourceTimeLevelsUtcAndRawSortSurviveReopen() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        CurrentDirectoryGuard directoryGuard;
        QVERIFY(QDir::setCurrent(temp.path()));
        const QDateTime base =
            QDateTime::fromString(QStringLiteral("2024-02-03T04:05:06.100Z"), Qt::ISODateWithMs);
        for (const auto &name : {QStringLiteral("later.txt"), QStringLiteral("earlier.txt")}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("data"), qint64(4));
            QVERIFY(file.flush());
            // Windows requires a writable handle to set FILE_WRITE_ATTRIBUTES.
            QVERIFY(file.setFileTime(base.addMSecs(name.startsWith('l') ? 800 : 0),
                                     QFileDevice::FileModificationTime));
            file.close();
        }
        {
            MainWindow window;
            window.show();
            auto *files = static_cast<FileTable *>(table(window));
            auto *viewMenu = window.findChild<QMenu *>("viewMenu");
            QVERIFY(viewMenu);
            QVERIFY(QMetaObject::invokeMethod(viewMenu, "aboutToShow", Qt::DirectConnection));
            for (int index = 0; index < 5; ++index) {
                auto *action = window.findChild<QAction *>(
                    QStringLiteral("timestampLevel%1Action").arg(index));
                QVERIFY(action);
                QVERIFY(!action->text().isEmpty());
            }
            files->sortItems(4, Qt::AscendingOrder);
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("earlier.txt"));
            auto *day = window.findChild<QAction *>("timestampLevel0Action");
            day->trigger();
            QCOMPARE(files->model()->index(0, 4).data().toString().size(), 10);
            QCOMPARE(files->model()->index(1, 4).data().toString(),
                     files->model()->index(0, 4).data().toString());
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("earlier.txt"));
            auto *utc = window.findChild<QAction *>("timestampUtcAction");
            utc->trigger();
            QVERIFY(files->model()->index(0, 4).data().toString().endsWith('Z'));
            auto *seconds = window.findChild<QAction *>("timestampLevel2Action");
            seconds->trigger();
            QVERIFY(
                files->model()->index(0, 4).data().toString().contains(QStringLiteral("04:05:06")));
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("earlier.txt"));
        }
        {
            MainWindow reopened;
            reopened.show();
            auto *files = static_cast<FileTable *>(table(reopened));
            QVERIFY(files->model()->index(0, 4).data().toString().endsWith('Z'));
            QVERIFY(
                files->model()->index(0, 4).data().toString().contains(QStringLiteral("04:05:06")));
            auto *viewMenu = reopened.findChild<QMenu *>("viewMenu");
            QVERIFY(QMetaObject::invokeMethod(viewMenu, "aboutToShow", Qt::DirectConnection));
            QVERIFY(reopened.findChild<QAction *>("timestampLevel2Action")->isChecked());
            QVERIFY(reopened.findChild<QAction *>("timestampUtcAction")->isChecked());
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                reopened.resize(size);
                QCoreApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*reopened.centralWidget(), reopened));
                QVERIFY(UiInspection::writeReport(
                    reopened, QStringLiteral("source-time-%1").arg(size.width())));
            }
        }
        {
            ControlledBackend backend;
            MainWindow archive(&backend);
            archive.show();
            ArchiveEntry entry;
            entry.path = QStringLiteral("архив с пробелом.txt");
            entry.modified = base.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
                             QStringLiteral(".1234567");
            openListedArchive(archive, backend, temp.filePath("time.7z"), {entry});
            QTRY_COMPARE(table(archive)->rowCount(), 1);
            auto *files = static_cast<FileTable *>(table(archive));
            QVERIFY(
                files->model()->index(0, 4).data().toString().contains(QStringLiteral("04:05:06")));
            QVERIFY(files->model()->index(0, 4).data().toString().endsWith('Z'));
            QVERIFY(QMetaObject::invokeMethod(archive.findChild<QMenu *>("viewMenu"), "aboutToShow",
                                              Qt::DirectConnection));
            archive.findChild<QAction *>("timestampLevel0Action")->trigger();
            QCOMPARE(files->model()->index(0, 4).data().toString().size(), 11);
        }
    }
};

QTEST_MAIN(PanelColumnsTests)
#include "panel_columns_tests.moc"
