#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/views/filetable.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class PanelFilesystemSortTests final : public QObject {
    Q_OBJECT
    static QString text(IFolderFolder *folder, UInt32 index, PROPID property) {
        NWindows::NCOM::CPropVariant value;
        if (folder->GetProperty(index, property, &value) != S_OK || value.vt != VT_BSTR)
            return {};
        return QString::fromWCharArray(value.bstrVal).normalized(QString::NormalizationForm_C);
    }
    static UInt32 find(IFolderFolder *folder, const QString &name) {
        UInt32 count = 0;
        folder->GetNumberOfItems(&count);
        for (UInt32 i = 0; i < count; ++i)
            if (text(folder, i, kpidName) == name)
                return i;
        return count;
    }
  private slots:
    void nameOrderFollowsRenameAndReload() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString current = QStringLiteral("имя space 2.txt");
        const QString other = QStringLiteral("имя space 10.txt");
        for (const QString &name : {current, other}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("unchanged"), qint64(9));
        }
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        CMyComPtr<IFolderCompare> compare;
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        QCOMPARE(folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        QCOMPARE(folder->LoadItems(), S_OK);
        auto first = find(folder, current);
        auto second = find(folder, other);
        QVERIFY(first < 2 && second < 2 && first != second);
        QVERIFY(compare->CompareItems(first, second, kpidName, 0) < 0);
        for (const auto &[renamed, sign] : {qMakePair(QStringLiteral("имя space 20.txt"), 1),
                                            qMakePair(QStringLiteral("имя space 1.txt"), -1)}) {
            QCOMPARE(operations->Rename(first, renamed.toStdWString().c_str(), nullptr), S_OK);
            QCOMPARE(folder->LoadItems(), S_OK);
            current = renamed;
            first = find(folder, current);
            second = find(folder, other);
            QVERIFY(first < 2 && second < 2 && first != second);
            const int order = compare->CompareItems(first, second, kpidName, 0);
            QVERIFY(sign > 0 ? order > 0 : order < 0);
            QFile file(temp.filePath(current));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("unchanged"));
        }
    }

    void sourceMetadataAndMissingFile() {
        QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/metadata файл space-XXXXXX"));
        QVERIFY(temp.isValid());
        QVERIFY(QDir(temp.path()).mkdir("folder"));
        const QStringList names = {QStringLiteral("file2.ext2"), QStringLiteral("file10.ext10")};
        for (int i = 0; i < names.size(); ++i) {
            QFile file(temp.filePath(names[i]));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(i + 1, 'x')), qint64(i + 1));
            QVERIFY(file.flush());
            QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1700000000 + i, QTimeZone::UTC),
                                     QFileDevice::FileModificationTime));
        }
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        const UInt32 first = find(folder, names[0]);
        const UInt32 second = find(folder, names[1]);
        const UInt32 directory = find(folder, "folder");
        QVERIFY(first < 3 && second < 3 && directory < 3);
        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        for (PROPID property : {kpidName, kpidSize, kpidPackSize, kpidMTime, kpidExtension}) {
            QVERIFY2(compare->CompareItems(first, second, property, 0) < 0,
                     qPrintable(QString("property %1").arg(property)));
            QVERIFY(compare->CompareItems(second, first, property, 0) > 0);
            QCOMPARE(compare->CompareItems(first, first, property, 0), 0);
        }
        QCOMPARE(compare->CompareItems(directory, first, kpidIsDir, 0), -1);
        QCOMPARE(compare->CompareItems(first, directory, kpidIsDir, 0), 1);
        QCOMPARE(compare->CompareItems(first, second, kpidPrefix, 0), 0);
        QCOMPARE(compare->CompareItems(first, second, kpidNumSubFiles, 0), 0);
        for (PROPID property : {kpidAttrib, kpidCTime, kpidATime, kpidINode, kpidLinks}) {
            NWindows::NCOM::CPropVariant a, b;
            QCOMPARE(folder->GetProperty(first, property, &a), S_OK);
            QCOMPARE(folder->GetProperty(second, property, &b), S_OK);
            QVERIFY(a.vt != VT_EMPTY && a.vt == b.vt);
            const int expected = a.Compare(b);
            const int actual = compare->CompareItems(first, second, property, 0);
            QCOMPARE(actual < 0   ? -1
                     : actual > 0 ? 1
                                  : 0,
                     expected < 0   ? -1
                     : expected > 0 ? 1
                                    : 0);
        }
        // A fresh enumeration has no identity cache. Failed source metadata reads sort as zero.
        QCOMPARE(folder->LoadItems(), S_OK);
        const UInt32 missing = find(folder, names[0]);
        const UInt32 existing = find(folder, names[1]);
        QVERIFY(QFile::remove(temp.filePath(names[0])));
        QVERIFY(compare->CompareItems(missing, existing, kpidLinks, 0) < 0);
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(folder->GetProperty(missing, kpidLinks, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
        // PackSize comparison does not attempt a filesystem read.
        QVERIFY(compare->CompareItems(missing, existing, kpidPackSize, 0) < 0);
    }
    void sourceCommentsAndPanelConsumer_data() {
        QTest::addColumn<bool>("flat");
        QTest::addColumn<bool>("descending");
        for (bool flat : {false, true})
            for (bool descending : {false, true})
                QTest::newRow(qPrintable(QString("flat%1-desc%2").arg(flat).arg(descending)))
                    << flat << descending;
    }
    void sourceCommentsAndPanelConsumer() {
        QFETCH(bool, flat);
        QFETCH(bool, descending);
        QTemporaryDir temp(QDir::tempPath() + QStringLiteral("/sort файл space-XXXXXX"));
        QVERIFY(temp.isValid());
        const QStringList names = {QStringLiteral("a file2.txt"), QStringLiteral("b file10.txt")};
        for (const QString &name : names) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("payload"), qint64(7));
        }
        QFile description(temp.filePath("descript.ion"));
        QVERIFY(description.open(QIODevice::WriteOnly));
        const QByteArray original =
            QByteArray("\xEF\xBB\xBF") + ("\"" + names[0] + "\" same" + QChar(4) + "z\r\n\"" +
                                          names[1] + "\" same" + QChar(4) + "a\r\n")
                                             .toUtf8();
        QCOMPARE(description.write(original), qint64(original.size()));
        description.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temp.path());
        CMyComPtr<IFolderSetFlatMode> mode;
        QCOMPARE(folder.QueryInterface(IID_IFolderSetFlatMode, &mode), S_OK);
        QCOMPARE(mode->SetFlatMode(flat), S_OK);
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        const UInt32 first = find(folder, names[0]);
        const UInt32 second = find(folder, names[1]);
        QVERIFY(first < 3 && second < 3 && first != second);
        QCOMPARE(text(folder, first, kpidComment), QStringLiteral("same"));
        QCOMPARE(text(folder, second, kpidComment), QStringLiteral("same"));

        // The displayed comments are equal; source comparison includes the metadata suffix.
        FileTable view(1);
        view.sourcePanel = &panel;
        view.textFolder = folder;
        view.setColumnProperties({kpidComment});
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        const QList<UInt32> indices = {first, second};
        for (int row = 0; row < 2; ++row) {
            FilePanelModel::Row item;
            item.sourceIndex = indices[row];
            view.panelModel()->append(std::move(item));
        }
        panel.AttachSelectionView(&view);
        view.selectRow(0);
        view.sortItems(0, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
        QCOMPARE(view.model()->index(0, 0).data(Qt::UserRole + 6).toUInt(),
                 descending ? first : second);
        CRecordVector<UInt32> selected;
        panel.Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), 1u);
        QCOMPARE(selected[0], first);

        CMyComPtr<IFolderCompare> compare;
        QCOMPARE(folder.QueryInterface(IID_IFolderCompare, &compare), S_OK);
        QVERIFY(compare->CompareItems(first, second, kpidComment, 0) > 0);
        QVERIFY(compare->CompareItems(second, first, kpidComment, 0) < 0);
        QVERIFY(compare->CompareItems(first, second, kpidName, 0) < 0);
        QCOMPARE(compare->CompareItems(first, second, kpidExtension, 0), 0);
        QCOMPARE(compare->CompareItems(first, second, kpidChangeTime, 0), 0);
        QCOMPARE(compare->CompareItems(first, first, kpidComment, 0), 0);

        // Sorting reads the source cache; GetProperty refreshes it lazily after reload.
        QVERIFY(QFile::remove(description.fileName()));
        QVERIFY(compare->CompareItems(first, second, kpidComment, 0) > 0);
        QCOMPARE(folder->LoadItems(), S_OK);
        const UInt32 reloadedFirst = find(folder, names[0]);
        const UInt32 reloadedSecond = find(folder, names[1]);
        QVERIFY(compare->CompareItems(reloadedFirst, reloadedSecond, kpidComment, 0) > 0);
        QCOMPARE(text(folder, reloadedFirst, kpidComment), QString());
        QCOMPARE(compare->CompareItems(reloadedFirst, reloadedSecond, kpidComment, 0), 0);
        for (const QString &name : names) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("payload"));
        }
    }
};

QTEST_MAIN(PanelFilesystemSortTests)
#include "panel_filesystem_sort_tests.moc"
