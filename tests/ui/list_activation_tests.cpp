#include "dialogs/listview/ListViewDialog.h"
#include "filemanager/views/qtlistview.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

class ListActivationTests : public QObject {
    Q_OBJECT
    QTemporaryDir _dir;
    void prepare(CListViewDialog &dialog, bool singleClick, unsigned columns = 1) {
        dialog.SettingsPath = _dir.filePath("настройки списка.ini");
        QSettings legacy(dialog.SettingsPath, QSettings::IniFormat);
        legacy.setValue("FM/SingleClick", singleClick);
        legacy.sync();
        dialog.NumColumns = columns;
        dialog.SelectFirst = true;
        for (int i = 0; i < 3; ++i) {
            dialog.Strings.Add(ToUString(QString("путь %1").arg(i)));
            dialog.Values.Add(ToUString(QString("значение %1\nполный текст").arg(i)));
        }
        dialog.OnInit();
        dialog.show();
        QApplication::processEvents();
    }
    static QPoint position(QTreeWidget *list, int row) {
        return list->visualItemRect(list->topLevelItem(row)).center();
    }
  private slots:
    void historyActivation_data() {
        QTest::addColumn<bool>("singleClick");
        QTest::newRow("single") << true;
        QTest::newRow("double") << false;
    }
    void historyActivation() {
        QFETCH(bool, singleClick);
        CListViewDialog dialog;
        prepare(dialog, singleClick);
        auto *list = dialog.findChild<QTreeWidget *>();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position(list, 1));
        QVERIFY(dialog.isVisible());
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, position(list, 1));
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.FocusedItemIndex, 1);
    }
    void modifiedClickOnlySelects() {
        CListViewDialog dialog;
        prepare(dialog, true);
        auto *list = dialog.findChild<QTreeWidget *>();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, position(list, 2));
        QCOMPARE(list->selectedItems().size(), 2);
        QVERIFY(dialog.isVisible());
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ShiftModifier, position(list, 0));
        QVERIFY(dialog.isVisible());
        dialog.reject();
        QCOMPARE(dialog.FocusedItemIndex, -1);
    }
    void doubleClickDetail_data() {
        QTest::addColumn<unsigned>("columns");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("properties") << 2u << Qt::KeyboardModifiers(Qt::NoModifier);
        QTest::newRow("alt-history") << 1u << Qt::KeyboardModifiers(Qt::AltModifier);
    }
    void doubleClickDetail() {
        QFETCH(unsigned, columns);
        QFETCH(Qt::KeyboardModifiers, modifiers);
        CListViewDialog dialog;
        prepare(dialog, true, columns);
        auto *list = dialog.findChild<QTreeWidget *>();
        int opened = 0;
        QTest::mouseClick(list->viewport(), Qt::LeftButton, modifiers, position(list, 1));
        QVERIFY(dialog.isVisible());
        QTimer::singleShot(0, &dialog, [&] {
            auto *detail = dialog.findChild<QDialog *>("listItemInfoDialog");
            QVERIFY(detail);
            QCOMPARE(detail->findChild<QPlainTextEdit *>()->toPlainText(),
                     columns == 1 ? QString("путь 1") : QString("значение 1\nполный текст"));
            ++opened;
            detail->reject();
        });
        QTest::mouseDClick(list->viewport(), Qt::LeftButton, modifiers, position(list, 1));
        if (modifiers.testFlag(Qt::AltModifier))
            QTest::keyRelease(list, Qt::Key_Alt);
        QCOMPARE(opened, 1);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.FocusedItemIndex, -1);
    }
};
QTEST_MAIN(ListActivationTests)
#include "list_activation_tests.moc"
