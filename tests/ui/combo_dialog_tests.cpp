#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/combo/ComboDialog.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QTimer>
#include <QtTest>

class ComboDialogTests : public QObject {
    Q_OBJECT
  private slots:
    void initialFrameScalesWithSourceFont() {
        const QFont originalFont = QApplication::font();
        const auto restoreFont = qScopeGuard([&] { QApplication::setFont(originalFont); });
        QSize smallFrame;
        for (int points : {8, 12}) {
            QFont font = originalFont;
            font.setPointSize(points);
            QApplication::setFont(font);
            CComboDialog dialog("Create Folder", "Folder name:", QStringLiteral("папка 空"));
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
            QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
            QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
            dialog.show();
            QApplication::processEvents();
            if (points == 8)
                smallFrame = dialog.size();
            else {
                QVERIFY(dialog.width() > smallFrame.width());
                QVERIFY(dialog.height() > smallFrame.height());
            }
            QVERIFY(UiInspection::writeReport(dialog, QString("combo-source-font-%1").arg(points)));
            dialog.reject();
            QCOMPARE(ToQString(dialog.Value), QStringLiteral("папка 空"));
        }
    }

    void initializesValueAndOrderedStringsWithoutReplacingEdit() {
        const QStringList strings = {"first", QStringLiteral("второе имя"), "third"};
        CComboDialog dialog("Заголовок", "Имя <буквально>:", "initial", nullptr, strings);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Заголовок"));
        auto *combo = dialog.findChild<QComboBox *>("comboValue");
        QCOMPARE(combo->count(), strings.size());
        for (int i = 0; i < strings.size(); ++i)
            QCOMPARE(combo->itemText(i), strings[i]);
        QCOMPARE(combo->currentText(), QStringLiteral("initial"));
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("initial"));
        auto *label = dialog.findChild<QLabel *>("comboPrompt");
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QCOMPARE(label->text(), QStringLiteral("Имя <буквально>:"));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QVERIFY(combo->hasFocus() || combo->lineEdit()->hasFocus());
        QCOMPARE(combo->lineEdit()->selectedText(), QStringLiteral("initial"));
    }

    void confirmationPreservesRawValue_data() {
        QTest::addColumn<bool>("button");
        QTest::addColumn<QString>("value");
        QTest::newRow("accept") << false << QStringLiteral("  имя с пробелом  ");
        QTest::newRow("button") << true << QStringLiteral("*.txt");
        QTest::newRow("empty") << false << QString();
    }
    void confirmationPreservesRawValue() {
        QFETCH(bool, button);
        QFETCH(QString, value);
        CComboDialog dialog("Title", "Prompt", "before");
        dialog.findChild<QComboBox *>("comboValue")->setCurrentText(value);
        if (button)
            dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        else
            dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), value);
    }

    void embeddedTerminatorKeepsDraft_data() {
        QTest::addColumn<bool>("retry");
        QTest::newRow("correct-and-confirm") << true;
        QTest::newRow("cancel") << false;
    }
    void embeddedTerminatorKeepsDraft() {
        QFETCH(bool, retry);
        CComboDialog dialog("Title", "Prompt", "before");
        auto *combo = dialog.findChild<QComboBox *>("comboValue");
        const QString draft = QStringLiteral("prefix") + QChar::Null + QStringLiteral("suffix");
        combo->setCurrentText(draft);
        dialog.show();
        bool error = false;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                error = !box->text().isEmpty();
                box->accept();
            }
        });
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QVERIFY(error);
        QVERIFY(dialog.isVisible());
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("before"));
        QCOMPARE(combo->currentText(), draft);
        if (retry) {
            combo->setCurrentText(QStringLiteral("  исправлено 空  "));
            dialog.accept();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(ToQString(dialog.Value), QStringLiteral("  исправлено 空  "));
        } else {
            dialog.reject();
            QCOMPARE(dialog.result(), int(QDialog::Rejected));
            QCOMPARE(ToQString(dialog.Value), QStringLiteral("before"));
        }
    }

    void cancelPreservesPublicValueAndFolderHelperClearsOutput() {
        CComboDialog dialog("Title", "Prompt", "before");
        dialog.findChild<QComboBox *>("comboValue")->setCurrentText("edited");
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("before"));
        QWidget parent;
        UString name = L"previous";
        QString initialName;
        bool found = false;
        QTimer::singleShot(0, &parent, [&] {
            if (auto *folder = parent.findChild<CComboDialog *>("comboDialog")) {
                found = true;
                initialName = ToQString(folder->Value);
                folder->reject();
            }
        });
        QVERIFY(!Dlg_CreateFolder(&parent, name));
        QVERIFY(found);
        QCOMPARE(initialName, QString("New Folder"));
        QVERIFY(name.IsEmpty());
        QTimer::singleShot(0, &parent, [&] {
            if (auto *folder = parent.findChild<CComboDialog *>("comboDialog")) {
                folder->findChild<QComboBox *>("comboValue")
                    ->setCurrentText(QStringLiteral("папка с пробелом"));
                folder->accept();
            }
        });
        QVERIFY(Dlg_CreateFolder(&parent, name));
        QCOMPARE(ToQString(name), QStringLiteral("папка с пробелом"));
    }

    void geometry_data() {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<int>("width");
        for (int width : {360, 260}) {
            QTest::newRow(qPrintable(QStringLiteral("folder-%1").arg(width)))
                << QString("Создать папку") << QString("Имя папки:") << width;
            QTest::newRow(qPrintable(QStringLiteral("file-%1").arg(width)))
                << QString("Создать файл") << QString("Имя файла:") << width;
            QTest::newRow(qPrintable(QStringLiteral("mask-%1").arg(width)))
                << QString("Выделить") << QString("Маска:") << width;
            QTest::newRow(qPrintable(QStringLiteral("long-prompt-%1").arg(width)))
                << QString("Создать папку")
                << QString("Введите имя новой папки для выбранного расположения:") << width;
        }
    }
    void geometry() {
        QFETCH(QString, title);
        QFETCH(QString, prompt);
        QFETCH(int, width);
        CComboDialog dialog(title, prompt, QStringLiteral("длинное имя с пробелами и Unicode"));
        dialog.resize(width, 150);
        dialog.show();
        QApplication::processEvents();
        auto *label = dialog.findChild<QLabel *>("comboPrompt");
        auto *combo = dialog.findChild<QComboBox *>("comboValue");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("comboButtons");
        QVERIFY(UiInspection::fullyVisible(*label, dialog));
        QVERIFY2(UiInspection::labelFits(*label),
                 qPrintable(QString("%1: %2x%3 required height %4")
                                .arg(label->objectName())
                                .arg(label->width())
                                .arg(label->height())
                                .arg(label->heightForWidth(label->width()))));
        QVERIFY(UiInspection::fullyVisible(*combo, dialog));
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(UiInspection::bounds(*label, dialog).bottom() <
                UiInspection::bounds(*combo, dialog).top());
        QVERIFY(UiInspection::separate(*combo, *buttons, dialog));
        QVERIFY(UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                       *buttons->button(QDialogButtonBox::Cancel), dialog));
        QVERIFY(
            UiInspection::writeReport(dialog, QStringLiteral("combo-%1-%2")
                                                  .arg(QString::fromLatin1(QTest::currentDataTag()))
                                                  .arg(width)));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ComboDialogTests)
#include "combo_dialog_tests.moc"
