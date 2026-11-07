#include "dialogs/overwrite/OverwriteDialog.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>

class OverwriteLayoutTests final : public QObject {
    Q_OBJECT

  private slots:
    void cancellationIgnoresScope() {
        for (int action = 0; action < 3; ++action) {
            COverwriteDialog dialog;
            QVERIFY(dialog.OnInit());
            dialog.show();
            QVERIFY(QTest::qWaitForWindowActive(&dialog));
            auto *apply = dialog.findChild<QCheckBox *>("overwriteApplyToRemaining");
            QVERIFY(apply && apply->isEnabled());
            apply->setChecked(true);
            if (action == 0)
                QTest::keyClick(&dialog, Qt::Key_Escape);
            else if (action == 1)
                dialog.close();
            else
                dialog.findChild<QPushButton *>("overwriteCancel")->click();
            QCOMPARE(dialog.result(), IDCANCEL);
        }
    }
    void decisionScopeAndLanguage_data() {
        QTest::addColumn<bool>("multiple");
        QTest::newRow("single") << false;
        QTest::newRow("multiple") << true;
    }
    void decisionScopeAndLanguage() {
        QFETCH(bool, multiple);
        QTemporaryDir settings;
        const QString ini = settings.filePath("language.ini");
        SaveRegLang(UString(L"-"), ini);
        ReloadLang(ini);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(UString(L"-"), ini);
            ReloadLang(ini);
            QtNotifyLanguageChanged();
        });
        const QString name = QStringLiteral("длинное имя 空 ") + QString(100, 'x') + ".txt";
        OverwriteQuestion question;
        question.Existing.Path = "/old/" + name;
        question.Incoming.Path = "/new/" + name;
        question.MultipleConflictsPossible = multiple;
        COverwriteDialog dialog;
        dialog.SetQuestion(question);
        dialog.OnInit();
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *apply = dialog.findChild<QCheckBox *>("overwriteApplyToRemaining");
        auto *skip = dialog.findChild<QPushButton *>("overwriteNo");
        QVERIFY(skip->hasFocus());
        QCOMPARE(apply->isVisible(), multiple);
        QVERIFY(dialog.findChild<QLabel *>("overwriteNewFile")->text().contains(name));
        QVERIFY(dialog.findChild<QLabel *>("overwriteNewFile")->text().contains("Unknown"));
        if (multiple) {
            QLabel *caption = nullptr;
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->buddy() == apply)
                    caption = label;
            QVERIFY(caption);
            QTest::mouseClick(caption, Qt::LeftButton);
            QVERIFY(apply->isChecked());
            const QString english = apply->accessibleName();
            SaveRegLang(UString(L"ru"), ini);
            ReloadLang(ini);
            QtNotifyLanguageChanged();
            QVERIFY(apply->isChecked());
            QVERIFY(apply->accessibleName() != english);
            QVERIFY(dialog.findChild<QLabel *>("overwriteNewFile")->text().contains(name));
            QVERIFY(dialog.findChild<QLabel *>("overwriteNewFile")
                        ->text()
                        .contains(QStringLiteral("Неизвестно")));
            skip->setFocus();
            QTest::keyClick(skip, Qt::Key_Return);
            QCOMPARE(dialog.result(), IDB_NO_TO_ALL);
        } else {
            dialog.findChild<QPushButton *>("overwriteRename")->click();
            QCOMPARE(dialog.result(), NOverwriteDialog::kAutoRenameOnceResult);
        }
    }
    void sourceFooterAndFileRows_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("width");
        QTest::addColumn<bool>("extra");
        for (const QString &language : {QString("-"), QString("ru")})
            for (int width : {700, 560, 380, 360})
                for (bool extra : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(language).arg(width).arg(extra)))
                        << language << width << extra;
    }

    void sourceFooterAndFileRows() {
        QFETCH(QString, language);
        QFETCH(int, width);
        QFETCH(bool, extra);
        QTemporaryDir settings;
        const QString ini = settings.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), ini);
        ReloadLang(ini);
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(UString(L"-"), ini);
            ReloadLang(ini);
            QtNotifyLanguageChanged();
        });
        COverwriteDialog dialog;
        dialog.ShowExtraButtons = extra;
        dialog.DefaultButton_is_NO = true;
        dialog.OldFileInfo.Path = L"/destination/каталог 文件/old.txt";
        dialog.NewFileInfo.Path = L"/source/каталог 文件/new.txt";
        dialog.OldFileInfo.SetSize(12345);
        dialog.NewFileInfo.SetSize(67890);
        QVERIFY(dialog.OnInit());
        dialog.resize(width, width < 500 ? 300 : 440);
        dialog.show();
        QApplication::processEvents();
        auto *yes = dialog.findChild<QPushButton *>("overwriteYes");
        auto *no = dialog.findChild<QPushButton *>("overwriteNo");
        auto *rename = dialog.findChild<QPushButton *>("overwriteRename");
        auto *cancel = dialog.findChild<QPushButton *>("overwriteCancel");
        auto *apply = dialog.findChild<QCheckBox *>("overwriteApplyToRemaining");
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(yes && no && rename && cancel && apply && scroll);
        QVERIFY(no->isDefault());
        QCOMPARE(rename->isVisible(), extra);
        QCOMPARE(apply->isVisible(), extra);
        QVERIFY(!apply->isChecked());
        const auto cancelCenter = cancel->mapTo(&dialog, cancel->rect().center());
        int previousRight = cancel->mapTo(&dialog, cancel->rect().topRight()).x();
        for (auto *button : {yes, no, rename}) {
            if (!button->isVisible())
                continue;
            QVERIFY(UiInspection::fullyVisible(*button, dialog));
            QVERIFY(button->width() >= button->sizeHint().width());
            QVERIFY(qAbs(button->mapTo(&dialog, button->rect().center()).y() - cancelCenter.y()) <=
                    1);
            QVERIFY(button->mapTo(&dialog, QPoint()).x() > previousRight);
            previousRight = button->mapTo(&dialog, button->rect().topRight()).x();
        }

        if (width < 500) {
            scroll->verticalScrollBar()->setValue(0);
            auto *header = dialog.findChild<QLabel *>("overwriteHeader");
            QTRY_VERIFY(UiInspection::fullyVisible(*header, dialog));
            QVERIFY(UiInspection::writeReport(
                dialog,
                QString("overwrite-layout-%1-%2-%3-top").arg(language).arg(width).arg(extra)));
        }
        for (auto *widget : dialog.findChildren<QWidget *>()) {
            if (!widget->isVisible() || widget == scroll || !scroll->widget()->isAncestorOf(widget))
                continue;
            const auto reveal = [&] {
                const QRect rect(widget->mapTo(scroll->widget(), QPoint()), widget->size());
                scroll->ensureVisible(rect.left(), rect.top(), 0, 0);
                scroll->ensureVisible(rect.right() + 1, rect.bottom() + 1, 0, 0);
                return UiInspection::fullyVisible(*widget, dialog);
            };
            if (widget->height() <= scroll->viewport()->height())
                QTRY_VERIFY2(reveal(), qPrintable(QString("%1 %2 %3,%4 %5x%6")
                                                      .arg(widget->metaObject()->className())
                                                      .arg(widget->objectName())
                                                      .arg(widget->x())
                                                      .arg(widget->y())
                                                      .arg(widget->width())
                                                      .arg(widget->height())));
            if (auto *label = qobject_cast<QLabel *>(widget))
                QVERIFY(UiInspection::labelFits(*label));
            if (auto *button = qobject_cast<QPushButton *>(widget))
                QVERIFY(button->width() >= button->sizeHint().width());
            QVERIFY(UiInspection::fullyVisible(*cancel, dialog));
        }
        QVERIFY(UiInspection::writeReport(
            dialog, QString("overwrite-layout-%1-%2-%3").arg(language).arg(width).arg(extra)));
        if (extra) {
            dialog.ShowExtraButtons = false;
            QVERIFY(dialog.OnInit());
            QVERIFY(!apply->isVisible());
            QVERIFY(!rename->isVisible());
            QVERIFY(yes->isEnabled() && no->isEnabled() && cancel->isEnabled());
        }
    }

    void initialFrameKeepsControlsReadableAtLargerFont() {
        const int points[] = {8, 12};
        for (int i = 0; i < 2; ++i) {
            COverwriteDialog dialog;
            QFont font = dialog.font();
            font.setPointSize(points[i]);
            dialog.setFont(font);
            dialog.OldFileInfo.Path = L"/old/file.txt";
            dialog.NewFileInfo.Path = L"/new/file.txt";
            dialog.OldFileInfo.SetSize(10);
            dialog.NewFileInfo.SetSize(20);
            QVERIFY(dialog.OnInit());
            dialog.show();
            QApplication::processEvents();
            for (auto *button : dialog.findChildren<QPushButton *>())
                if (button->isVisible()) {
                    QVERIFY(UiInspection::fullyVisible(*button, dialog));
                    QVERIFY(button->width() >= button->sizeHint().width());
                }
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->isVisible())
                    QVERIFY(UiInspection::labelFits(*label));
            QVERIFY(UiInspection::writeReport(dialog,
                                              QString("overwrite-initial-font-%1").arg(points[i])));
        }
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(OverwriteLayoutTests)
#include "overwrite_layout_tests.moc"
