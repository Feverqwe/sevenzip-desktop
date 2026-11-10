#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/properties/qtpropertyformat.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/support/layout_inspection.h"
#include <QTimeZone>

#include <QAbstractItemModelTester>
#include <QHeaderView>
#include <QTextDocument>

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

    void hidingCurrentColumnKeepsTheFileSelected_data() {
        QTest::addColumn<bool>("typeColumn");
        QTest::newRow("size") << false;
        QTest::newRow("type") << true;
    }

    void hidingCurrentColumnKeepsTheFileSelected() {
        QFETCH(bool, typeColumn);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QFile file(temp.filePath("payload.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("large"), qint64(5));
        file.close();
        QFile first(temp.filePath("alpha.txt"));
        QVERIFY(first.open(QIODevice::WriteOnly));
        first.close();
        MainWindow window;
        window.show();
        window.openPath(temp.path());
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && filesystemRow(window, "payload.txt") >= 0);
        int column = files->columnForProperty(kpidSize);
        if (typeColumn) {
            column = -1;
            const QString title = QCoreApplication::translate("QtWorkspaceFileTable", "Type");
            for (int candidate = 0; candidate < files->columnCount(); ++candidate)
                if (!files->isColumnHidden(candidate) &&
                    files->model()->headerData(candidate, Qt::Horizontal).toString() == title) {
                    column = candidate;
                    break;
                }
        }
        QVERIFY(column > 0 && !files->isColumnHidden(column));
        if (!typeColumn) {
            files->sortItems(column, Qt::DescendingOrder);
            QVERIFY(filesystemRow(window, "payload.txt") < filesystemRow(window, "alpha.txt"));
        }
        const int row = filesystemRow(window, "payload.txt");
        files->selectRow(row);
        files->setCurrentCell(row, column, QItemSelectionModel::NoUpdate);
        const QString label = files->model()->headerData(column, Qt::Horizontal).toString();
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (QAction *option : menu->actions())
                if (option->isChecked() && option->text() == label) {
                    option->trigger();
                    break;
                }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(files->horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        QVERIFY(files->isColumnHidden(column));
        QCOMPARE(files->currentIndex().data().toString(), QString("payload.txt"));
        QVERIFY(files->selectionModel()->isRowSelected(files->currentRow(), {}));
        if (!typeColumn) {
            QVERIFY(filesystemRow(window, "alpha.txt") < filesystemRow(window, "payload.txt"));
            action(window, "sort3Action")->trigger();
            QVERIFY(filesystemRow(window, "payload.txt") < filesystemRow(window, "alpha.txt"));
            QVERIFY(files->isColumnHidden(column));

            QFile middle(temp.filePath("middle.txt"));
            QVERIFY(middle.open(QIODevice::WriteOnly));
            QCOMPARE(middle.write("mid"), qint64(3));
            middle.close();
            action(window, "refreshAction")->trigger();
            QTRY_VERIFY(filesystemRow(window, "middle.txt") >= 0);
            const int payload = filesystemRow(window, "payload.txt");
            QVERIFY(payload >= 0);
            QVERIFY(payload < filesystemRow(window, "middle.txt"));
            QVERIFY(filesystemRow(window, "middle.txt") < filesystemRow(window, "alpha.txt"));
            QVERIFY(files->isColumnHidden(files->columnForProperty(kpidSize)));
            QCOMPARE(files->currentRow(), payload);
            QVERIFY(files->selectionModel()->isRowSelected(payload, {}));
        }
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
            accessTimeLabel =
                files->model()->headerData(accessTimeColumn, Qt::Horizontal).toString();
            QTimer::singleShot(0, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (QAction *option : menu->actions())
                    if (!option->isChecked() &&
                        (option->text() == accessTimeLabel ||
                         option->text() == files->model()
                                               ->headerData(createdTimeColumn, Qt::Horizontal)
                                               .toString()))
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
                for (int mode : {0, 3}) {
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
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (QAction *option : menu->actions())
                if (!option->isChecked() &&
                    option->text() ==
                        files->model()->headerData(packedSize, Qt::Horizontal).toString())
                    option->trigger();
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(header, "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        QVERIFY(!files->isColumnHidden(packedSize));
        QVERIFY(files->columnForProperty(kpidNumSubFiles) >= 0);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QVERIFY(files->isColumnHidden(diskSize));
    }

    void systemDatesAndRawSortSurviveReopen() {
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
            files->sortItems(4, Qt::AscendingOrder);
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("earlier.txt"));
            const QString expected =
                QLocale::system().toString(base.toLocalTime(), QLocale::ShortFormat);
            QCOMPARE(files->model()->index(0, 4).data().toString(), expected);
            QVERIFY(files->model()->index(0, 4).data(Qt::ToolTipRole).toString().contains(".100"));
            QVERIFY(files->model()->index(1, 4).data(Qt::ToolTipRole).toString().contains(".900"));
            SaveRegLang(UString("ru"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
            QCOMPARE(files->model()->index(0, 4).data().toString(), expected);
            QEvent region(QEvent::LocaleChange);
            QCoreApplication::sendEvent(files, &region);
            QCOMPARE(files->model()->index(0, 4).data().toString(), expected);
            files->sortItems(4, Qt::DescendingOrder);
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("later.txt"));
            files->sortItems(4, Qt::AscendingOrder);
            QCOMPARE(files->model()->index(0, 0).data().toString(), QStringLiteral("earlier.txt"));
        }
        {
            MainWindow reopened;
            reopened.show();
            auto *files = static_cast<FileTable *>(table(reopened));
            files->sortItems(4, Qt::AscendingOrder);
            QCOMPARE(files->model()->index(0, 4).data().toString(),
                     QLocale::system().toString(base.toLocalTime(), QLocale::ShortFormat));
            QVERIFY(files->model()->index(0, 4).data(Qt::ToolTipRole).toString().contains(".100"));
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
            entry.path = QStringLiteral("<b>архив с  пробелом & именем.txt");
            entry.modifiedTime.emplace();
            const UInt64 ticks =
                UInt64(base.toSecsSinceEpoch() + 11644473600LL) * 10000000 + 1234567;
            entry.modifiedTime->SetAsTimeFrom_Ft64_Prec(ticks, k_PropVar_TimePrec_100ns);
            openListedArchive(archive, backend, temp.filePath("time.7z"), {entry});
            QTRY_COMPARE(table(archive)->rowCount(), 1);
            auto *files = static_cast<FileTable *>(table(archive));
            QCOMPARE(files->model()->index(0, 4).data().toString(),
                     QLocale::system().toString(base.toLocalTime(), QLocale::ShortFormat));
            QTextDocument tooltip;
            tooltip.setHtml(files->model()->index(0, 4).data(Qt::ToolTipRole).toString());
            const auto lines = tooltip.toPlainText().split(u'\n');
            QCOMPARE(lines.front(), entry.path);
            QVERIFY(lines.last().contains(".1234567 UTC"));
        }
    }
};

QTEST_MAIN(PanelColumnsTests)
#include "panel_columns_tests.moc"
