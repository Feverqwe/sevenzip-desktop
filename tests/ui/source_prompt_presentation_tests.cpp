#include "dialogs/edit/EditDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QTimer>
#include <QtTest>

class SourcePromptPresentationTests final : public QObject {
    Q_OBJECT

    static void inspect(QDialog &view, bool resizable, const QString &tag) {
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(view, tag));
        QCOMPARE(view.windowFlags().testFlag(Qt::WindowMinMaxButtonsHint), resizable);
        QVERIFY(!view.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
        for (auto *button : view.findChildren<QPushButton *>()) {
            QVERIFY2(UiInspection::fullyVisible(*button, view), qPrintable(button->objectName()));
            QVERIFY(button->width() >= button->sizeHint().width());
        }
        for (auto *label : view.findChildren<QLabel *>()) {
            if (!label->isVisible())
                continue;
            QVERIFY2(UiInspection::fullyVisible(*label, view), qPrintable(label->objectName()));
            QVERIFY2(UiInspection::labelFits(*label),
                     qPrintable(QString("%1: %2x%3 required height %4")
                                    .arg(label->objectName())
                                    .arg(label->width())
                                    .arg(label->height())
                                    .arg(label->heightForWidth(label->width()))));
        }
    }

  private slots:
    void sourceInitialFrame_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("kind");
        for (const char *language : {"-", "ru"})
            for (int kind = 0; kind < 2; ++kind)
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(kind)))
                    << QString::fromLatin1(language) << kind;
    }
    void sourceInitialFrame() {
        QFETCH(QString, language);
        QFETCH(int, kind);
        SaveRegLang(UString(language.toStdWString().c_str()),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        const QString tag = QString("source-prompt-initial-%1-%2").arg(language).arg(kind);
        if (kind == 0) {
            CEditDialog view;
            view.Title = "Source detail";
            view.Text = L"Literal <value> 文件\nsecond line";
            view.OnInit();
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            inspect(view, true, tag);
            auto *close = view.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close);
            QCOMPARE(view.focusWidget(), close);
            QVERIFY(close->isDefault());
            QVERIFY(UiInspection::fullyVisible(*view.findChild<QPlainTextEdit *>(), view));
        } else if (kind == 1) {
            CPasswordDialog view;
            const QString archive = QStringLiteral("/fixtures/закрытый 空.7z");
            view.OnInit(archive, true);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            inspect(view, false, tag);
            auto *source = view.findChild<QWidget *>("passwordArchiveSource");
            QVERIFY(source && UiInspection::fullyVisible(*source, view));
            QCOMPARE(source->toolTip(), archive);
            QCOMPARE(view.focusWidget(), view.findChild<QLineEdit *>("passwordEdit"));
            const QRect show =
                UiInspection::bounds(*view.findChild<QCheckBox *>("passwordShow"), view);
            const QRect footer = view.layout()->itemAt(view.layout()->count() - 1)->geometry();
            QVERIFY(footer.top() > show.bottom());
        }
    }
    void cleanup() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourcePromptPresentationTests)
#include "source_prompt_presentation_tests.moc"
