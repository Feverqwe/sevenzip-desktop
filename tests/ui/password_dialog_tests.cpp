#include "dialogs/password/PasswordDialog.h"
#include "support/secret_visibility.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

class PasswordDialogTests final : public QObject {
    Q_OBJECT
  private slots:
    void revealPreservesEditingPosition() {
        CPasswordDialog dialog;
        dialog.OnInit();
        SecretVisibility::verify(dialog, *dialog.findChild<QLineEdit *>("passwordEdit"),
                                 *dialog.findChild<QCheckBox *>("passwordShow"));
    }
    void initialValueAndShowTransitions() {
        CPasswordDialog dialog;
        const QString password = QStringLiteral("пароль 密碼 с пробелом");
        dialog.Password = password.toStdWString().c_str();
        dialog.OnInit();
        dialog.show();
        auto *edit = dialog.findChild<QLineEdit *>("passwordEdit");
        auto *show = dialog.findChild<QCheckBox *>("passwordShow");
        QCOMPARE(edit->text(), password);
        QCOMPARE(edit->echoMode(), QLineEdit::Password);
        edit->setText(password + " changed");
        show->click();
        QCOMPARE(edit->echoMode(), QLineEdit::Normal);
        QCOMPARE(edit->text(), password + " changed");
        QVERIFY(dialog.ShowPassword);
        show->click();
        QCOMPARE(edit->echoMode(), QLineEdit::Password);
        QCOMPARE(edit->text(), password + " changed");
        QVERIFY(!dialog.ShowPassword);
        QVERIFY(!UiInspection::dump(dialog).contains(password));
    }
    void okEnterAndCancel() {
        for (bool enter : {false, true}) {
            CPasswordDialog dialog;
            dialog.ShowPassword = true;
            dialog.OnInit();
            dialog.show();
            auto *edit = dialog.findChild<QLineEdit *>("passwordEdit");
            QCOMPARE(edit->echoMode(), QLineEdit::Normal);
            edit->setText(QStringLiteral("  密碼  "));
            if (enter)
                QTest::keyClick(edit, Qt::Key_Return);
            else
                dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            QCOMPARE(dialog.result(), int(QDialog::Accepted));
            QCOMPARE(QString::fromWCharArray(dialog.Password), QStringLiteral("  密碼  "));
        }
        CPasswordDialog dialog;
        dialog.Password = "initial";
        dialog.OnInit();
        dialog.findChild<QLineEdit *>("passwordEdit")->setText("discarded");
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(QString::fromWCharArray(dialog.Password), QString("initial"));
    }
    void geometry() {
        for (int width : {360, 260}) {
            for (bool shown : {false, true}) {
                CPasswordDialog dialog;
                dialog.ShowPassword = shown;
                dialog.OnInit();
                dialog.resize(width, 170);
                dialog.show();
                QApplication::processEvents();
                auto *prompt = dialog.findChild<QLabel *>("passwordPrompt");
                auto *edit = dialog.findChild<QLineEdit *>("passwordEdit");
                auto *show = dialog.findChild<QCheckBox *>("passwordShow");
                auto *buttons = dialog.findChild<QDialogButtonBox *>();
                for (QWidget *control : QList<QWidget *>{prompt, edit, show, buttons})
                    QVERIFY(UiInspection::fullyVisible(*control, dialog));
                QVERIFY(UiInspection::labelFits(*prompt));
                QVERIFY(UiInspection::bounds(*prompt, dialog).bottom() <
                        UiInspection::bounds(*edit, dialog).top());
                QVERIFY(UiInspection::bounds(*edit, dialog).bottom() <
                        UiInspection::bounds(*show, dialog).top());
                QVERIFY(UiInspection::bounds(*show, dialog).bottom() <
                        UiInspection::bounds(*buttons, dialog).top());
                QVERIFY(UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                               *buttons->button(QDialogButtonBox::Cancel), dialog));
                QVERIFY(UiInspection::writeReport(dialog,
                                                  QString("password-%1-%2").arg(width).arg(shown)));
            }
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PasswordDialogTests)
#include "password_dialog_tests.moc"
