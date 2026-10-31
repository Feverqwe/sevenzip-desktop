#include "7zip/PropID.h"
#include "Windows/PropVariant.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/qtlocationfolder.h"
#include <QFile>
#include <QMessageBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
class ExternalFiles final : public IQtExternalFileServices {
  public:
    QStringList paths;
    HRESULT StartApplication(const UString &, const UString &path, QWidget *) override {
        paths << ToQString(path).normalized(QString::NormalizationForm_C);
        return S_OK;
    }
};

bool writeFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}

void populate(CPanel &panel, QTableWidget &table) {
    panel.AttachSelectionView(&table);
    // The source load prepares marks before this fixture publishes Qt rows.
    QCOMPARE(panel.LoadListItems(), S_OK);
    UInt32 count = 0;
    QCOMPARE(panel.GetFolder()->GetNumberOfItems(&count), S_OK);
    table.setRowCount(int(count));
    for (UInt32 index = 0; index < count; ++index) {
        NWindows::NCOM::CPropVariant name;
        QCOMPARE(panel.GetFolder()->GetProperty(index, kpidName, &name), S_OK);
        QCOMPARE(name.vt, VARTYPE(VT_BSTR));
        const QString nativeName = ToQString(UString(name.bstrVal));
        auto *item = new QTableWidgetItem(nativeName.normalized(QString::NormalizationForm_C));
        item->setData(Qt::UserRole + 3, nativeName);
        item->setData(Qt::UserRole + 6, index);
        table.setItem(int(index), 0, item);
    }
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void bind(CPanel &panel, const QString &path) {
    CMyComPtr<IFolderFolder> folder;
    QCOMPARE(CreateFilesystemFolder(path, &folder), S_OK);
    panel.SetNewFolder(folder);
    panel.LoadFullPath();
}
} // namespace

class NativeOpenCommandsTests : public QObject {
    Q_OBJECT
  private slots:
    void outsideOpensAllOperatedFilesUsingNativeIndices() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const QString &name : {QStringLiteral("первый й.txt"), QStringLiteral("second.txt")})
            QVERIFY(writeFile(fixture.filePath(name)));
        QTableWidget table(0, 1);
        CPanel panel;
        ExternalFiles services;
        panel.AttachExternalFileServices(&services);
        bind(panel, fixture.path());
        populate(panel, table);
        table.selectAll();
        table.setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        // Reverse presentation rows: source item parameters, not visual indices,
        // own the operation. Forged display names must not reach the service.
        table.item(0, 0)->setData(Qt::UserRole + 6, 1u);
        table.item(1, 0)->setData(Qt::UserRole + 6, 0u);
        table.item(0, 0)->setText("forged.txt");
        panel.OpenSelectedItems(false);
        services.paths.sort();
        QStringList expected = {fixture.filePath(QStringLiteral("первый й.txt")),
                                fixture.filePath(QStringLiteral("second.txt"))};
        expected.sort();
        QStringList actual = services.paths;
        for (QString &path : actual)
            path = QDir::fromNativeSeparators(path);
        QCOMPARE(actual, expected);
    }
    void insideUsesFocusedFolderInsteadOfOtherSelectedFile() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir(fixture.path()).mkdir(QStringLiteral("папка й")));
        QVERIFY(writeFile(fixture.filePath("selected.txt")));
        QTableWidget table(0, 1);
        CPanel panel;
        bind(panel, fixture.path());
        populate(panel, table);
        const auto folder = table.findItems(QStringLiteral("папка й"), Qt::MatchExactly);
        const auto file = table.findItems("selected.txt", Qt::MatchExactly);
        QCOMPARE(folder.size(), 1);
        QCOMPARE(file.size(), 1);
        table.selectRow(file.first()->row());
        table.setCurrentCell(folder.first()->row(), 0, QItemSelectionModel::NoUpdate);
        panel.OpenFocusedItemAsInternal(nullptr);
        QCOMPARE(QDir::cleanPath(ToQString(panel.GetCurrentFolderPrefix()))
                     .normalized(QString::NormalizationForm_C),
                 fixture.filePath(QStringLiteral("папка й")));
        QCOMPARE(table.rowCount(), 0);
    }
    void sourceTwentyItemLimitPrecedesEveryExternalSideEffect() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < 21; ++i)
            QVERIFY(writeFile(fixture.filePath(QStringLiteral("item %1.txt").arg(i))));
        QTableWidget table(0, 1);
        CPanel panel;
        ExternalFiles services;
        panel.AttachExternalFileServices(&services);
        bind(panel, fixture.path());
        populate(panel, table);
        table.selectAll();
        bool sawError = false;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &table, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box && box->isVisible() && box->objectName() == "panelArchiveError") {
                sawError = true;
                box->accept();
            }
        });
        answer.start(1);
        panel.OpenSelectedItems(false);
        answer.stop();
        QVERIFY(sawError);
        QVERIFY(services.paths.isEmpty());
    }
};

QTEST_MAIN(NativeOpenCommandsTests)
#include "native_open_commands_tests.moc"
