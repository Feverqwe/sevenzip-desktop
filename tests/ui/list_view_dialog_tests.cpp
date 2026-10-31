#include "dialogs/listview/ListViewDialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/dialogs/qtdialogunits.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>
#include <memory>

class ListViewDialogTests : public QObject {
    Q_OBJECT
  private slots:
    void sourceFrame_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<unsigned>("columns");
        for (const char *language : {"-", "ru"})
            for (unsigned columns : {1u, 2u})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(columns)))
                    << QString::fromLatin1(language) << columns;
    }
    void sourceFrame() {
        QFETCH(QString, language);
        QFETCH(unsigned, columns);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        CListViewDialog dialog;
        dialog.Title = "Source list fixture";
        dialog.NumColumns = columns;
        dialog.Strings.Add(UString(L"Unicode 文件"));
        dialog.Values.Add(UString("value"));
        dialog.OnInit();
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(
            dialog, QString("list-source-initial-%1-%2").arg(language).arg(columns)));
        QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinMaxButtonsHint));
        auto *list = dialog.findChild<QTreeWidget *>();
        QVERIFY(list);
        QCOMPARE(list->isHeaderHidden(), columns == 1);
        for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
            dialog.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*list, dialog));
            for (auto *button : dialog.findChild<QDialogButtonBox *>()->buttons())
                QVERIFY(UiInspection::fullyVisible(*button, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog,
                QString("list-source-%1-%2-%3").arg(language).arg(columns).arg(size.width())));
        }
    }
    void cleanup() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }

    void twoColumnsNormalizeAndTruncateDisplayOnly() {
        auto *clipboard = QApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        if (clipboard->mimeData())
            for (const QString &type : clipboard->mimeData()->formats())
                saved->setData(type, clipboard->mimeData()->data(type));
        const auto restore = qScopeGuard([&] { clipboard->setMimeData(saved.release()); });
        CListViewDialog dialog;
        dialog.NumColumns = 2;
        dialog.Strings.Add(UString("multiline"));
        dialog.Values.Add(UString(L"first\r\nsecond\nthird"));
        dialog.Strings.Add(UString("long"));
        const QString raw = QString(1100, QChar('x')) + QStringLiteral("😀 tail");
        dialog.Values.Add(ToUString(raw));
        dialog.Strings.Add(UString("missing value"));
        dialog.OnInit();
        auto *list = dialog.findChild<QTreeWidget *>();
        QCOMPARE(list->columnCount(), 2);
        QCOMPARE(list->topLevelItem(0)->text(1), QString("first second third"));
        QCOMPARE(list->topLevelItem(1)->text(1), QString(1024, QChar('x')) + " ...");
        QVERIFY(list->topLevelItem(2)->text(1).isEmpty());
        QCOMPARE(ToQString(dialog.Values[1]), raw);
        list->setCurrentItem(list->topLevelItem(1));
        QTest::keyClick(list, Qt::Key_Insert, Qt::ControlModifier);
#ifdef _WIN32
        QCOMPARE(QApplication::clipboard()->text(), "long: " + raw + "\r\n");
#else
        QCOMPARE(QApplication::clipboard()->text(), "long: " + raw + "\n");
#endif
    }

    void copyButtonUsesFullValuesAndPreservesSelection() {
        auto *clipboard = QApplication::clipboard();
        auto saved = std::make_unique<QMimeData>();
        if (clipboard->mimeData())
            for (const QString &type : clipboard->mimeData()->formats())
                saved->setData(type, clipboard->mimeData()->data(type));
        const auto restore = qScopeGuard([&] { clipboard->setMimeData(saved.release()); });
        CListViewDialog dialog;
        dialog.NumColumns = 2;
        dialog.DeleteIsAllowed = true;
        const QString raw = QString(1200, QChar('x')) + QStringLiteral("\n😀 tail");
        dialog.Strings.Add(UString("long"));
        dialog.Values.Add(ToUString(raw));
        dialog.Strings.Add(UString("other"));
        dialog.Values.Add(UString("value"));
        dialog.OnInit();
        dialog.show();
        auto *list = dialog.findChild<QTreeWidget *>();
        auto *copy = dialog.findChild<QPushButton *>("copyReportButton");
        const QString newline =
#ifdef _WIN32
            "\r\n";
#else
            "\n";
#endif
        copy->click();
        QCOMPARE(clipboard->text(), "long: " + raw + newline + "other: value" + newline);
        QVERIFY(list->selectedItems().isEmpty());
        list->setCurrentItem(list->topLevelItem(0));
        copy->click();
        QCOMPARE(clipboard->text(), "long: " + raw + newline);
        QCOMPARE(list->selectedItems().size(), 1);
        QTest::keyClick(list, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(list, Qt::Key_Delete);
        clipboard->setText("keep clipboard");
        copy->click();
        QCOMPARE(clipboard->text(), QString("keep clipboard"));
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void enterInPropertiesOpensFullValueWithoutAcceptingList() {
        CListViewDialog dialog;
        dialog.NumColumns = 2;
        dialog.SelectFirst = true;
        dialog.Strings.Add(UString("Name"));
        const QString raw = QString(1200, QChar('v')) + "\nnext line";
        dialog.Values.Add(ToUString(raw));
        dialog.OnInit();
        dialog.show();
        auto *list = dialog.findChild<QTreeWidget *>();
        bool checked = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *info = dialog.findChild<QDialog *>("listItemInfoDialog");
            QVERIFY(info);
            QCOMPARE(info->windowTitle(), QString("Name"));
            auto *text = info->findChild<QPlainTextEdit *>();
            QVERIFY(text && text->isReadOnly());
            QCOMPARE(text->toPlainText(), raw);
            checked = true;
            info->reject();
        });
        QTest::keyClick(list, Qt::Key_Return);
        QVERIFY(checked);
        QVERIFY(dialog.isVisible());
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.FocusedItemIndex, 0);
    }

    void deleteKeepsParallelStringsAndValuesAligned() {
        CListViewDialog dialog;
        dialog.NumColumns = 2;
        dialog.DeleteIsAllowed = true;
        for (int i = 0; i < 4; ++i) {
            dialog.Strings.Add(ToUString(QString::number(i)));
            dialog.Values.Add(ToUString(QString("value %1").arg(i)));
        }
        dialog.OnInit();
        auto *list = dialog.findChild<QTreeWidget *>();
        list->topLevelItem(1)->setSelected(true);
        list->topLevelItem(3)->setSelected(true);
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(dialog.Strings.Size(), 2u);
        QCOMPARE(dialog.Values.Size(), 2u);
        QCOMPARE(ToQString(dialog.Strings[0]), QString("0"));
        QCOMPARE(ToQString(dialog.Values[0]), QString("value 0"));
        QCOMPARE(ToQString(dialog.Strings[1]), QString("2"));
        QCOMPARE(ToQString(dialog.Values[1]), QString("value 2"));
        QVERIFY(dialog.StringsWereChanged);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ListViewDialogTests)
#include "list_view_dialog_tests.moc"
