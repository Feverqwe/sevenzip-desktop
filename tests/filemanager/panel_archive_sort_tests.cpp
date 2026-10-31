#include "archive/agent/Agent.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/views/filetable.h"
#include <QtTest>

class PanelArchiveSortTests final : public QObject {
    Q_OBJECT
  private slots:
    void sourceFolderProperties_data() {
        QTest::addColumn<quint32>("property");
        QTest::addColumn<bool>("flat");
        QTest::addColumn<int>("expected");
        for (bool flat : {false, true})
            for (quint32 property :
                 {quint32(kpidName), quint32(kpidExtension), quint32(kpidPrefix),
                  quint32(kpidIsDir), quint32(kpidSize), quint32(kpidPackSize), quint32(kpidCRC),
                  quint32(kpidNumSubDirs), quint32(kpidNumSubFiles), quint32(kpidMTime)}) {
                int expected = 0;
                if (property == kpidSize || property == kpidPackSize ||
                    (property == kpidName && !flat) || (property == kpidPrefix && flat) ||
                    (property == kpidMTime && flat))
                    expected = -1;
                if (property == kpidCRC)
                    expected = 1;
                QTest::newRow(qPrintable(QString("%1-%2").arg(property).arg(flat)))
                    << property << flat << expected;
            }
    }
    void sourceFolderProperties() {
        QFETCH(quint32, property);
        QFETCH(bool, flat);
        QFETCH(int, expected);
        ArchiveEntry first{QStringLiteral("dir2/common 文件.txt"), 7, 3, "2026-10-01 12:00:00"};
        first.crc = 2;
        ArchiveEntry second{QStringLiteral("dir10/common 文件.txt"), 11, 5, "2026-10-02 12:00:00"};
        second.crc = 1;
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({first, second}, UString(L"7z"), &folder), S_OK);
        CMyComPtr<IFolderSetFlatMode> mode;
        QCOMPARE(folder.QueryInterface(IID_IFolderSetFlatMode, &mode), S_OK);
        QCOMPARE(mode->SetFlatMode(flat), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        auto *spec = dynamic_cast<CAgentFolder *>(folder.Interface());
        QVERIFY(spec);
        QList<UInt32> indices;
        for (const auto &entry : {first, second}) {
            const QString path = flat ? entry.path : entry.path.section('/', 0, 0);
            bool found = false;
            for (UInt32 i = 0; i < spec->GetNumberOfItems(); ++i)
                if (QDir::fromNativeSeparators(
                        ToQString(spec->GetFullPrefix(i) + spec->GetName(i))) == path) {
                    indices.append(i);
                    found = true;
                    break;
                }
            QVERIFY(found);
        }
        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        const Int32 result = compare->CompareItems(indices[0], indices[1], property, 0);
        QCOMPARE(result < 0 ? -1 : result > 0 ? 1 : 0, expected);
        const Int32 reverse = compare->CompareItems(indices[1], indices[0], property, 0);
        QCOMPARE(reverse < 0 ? -1 : reverse > 0 ? 1 : 0, -expected);
    }
    void rawArchiveColumns_data() {
        QTest::addColumn<quint32>("property");
        QTest::addColumn<bool>("flat");
        QTest::addColumn<bool>("descending");
        for (quint32 property : {quint32(kpidSha1), quint32(kpidChecksum)})
            for (bool flat : {false, true})
                for (bool descending : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(property).arg(flat).arg(descending)))
                        << property << flat << descending;
    }
    void rawArchiveColumns() {
        QFETCH(quint32, property);
        QFETCH(bool, flat);
        QFETCH(bool, descending);
        QList<ArchiveEntry> entries;
        for (const auto &entry : QList<QPair<QString, QByteArray>>{
                 {QStringLiteral("a 文件.txt"), QByteArray::fromHex("ff")},
                 {QStringLiteral("b δ.txt"), QByteArray::fromHex("01")},
                 {QStringLiteral("c space.txt"), QByteArray::fromHex("0100")}}) {
            ArchiveEntry item;
            item.path = entry.first;
            item.rawProperties[property] = entry.second;
            entries.append(item);
        }
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder(entries, UString(L"7z"), &folder), S_OK);
        CMyComPtr<IFolderSetFlatMode> mode;
        QCOMPARE(folder.QueryInterface(IID_IFolderSetFlatMode, &mode), S_OK);
        QCOMPARE(mode->SetFlatMode(flat), S_OK);
        CPanel panel(folder);
        if (flat)
            panel.ChangeFlatMode();
        QCOMPARE(panel.LoadListItems(), S_OK);
        FileTable view(1);
        view.sourcePanel = &panel;
        view.textFolder = folder;
        view.setColumnProperties({property}, true);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        for (int row = 0; row < entries.size(); ++row) {
            FilePanelModel::Row item;
            item.sourceIndex = row;
            view.panelModel()->append(std::move(item));
        }
        panel.AttachSelectionView(&view);
        view.selectRow(1);
        CSelectedState selection;
        panel.SaveSelectedState(selection);
        view.sortItems(0, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
        QList<quint32> order;
        for (int row = 0; row < view.rowCount(); ++row)
            order.append(view.model()->index(row, 0).data(Qt::UserRole + 6).toUInt());
        QCOMPARE(order, descending ? QList<quint32>({0, 2, 1}) : QList<quint32>({1, 2, 0}));
        CRecordVector<UInt32> selected;
        panel.Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), 1u);
        QCOMPARE(selected[0], UInt32(1));
        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        QVERIFY(compare->CompareItems(1, 0, property, 1) < 0);
        QVERIFY(compare->CompareItems(1, 2, property, 1) < 0);
        QCOMPARE(compare->CompareItems(1, 1, property, 1), 0);
    }
};

QTEST_MAIN(PanelArchiveSortTests)
#include "panel_archive_sort_tests.moc"
