#include "dialogs/delete/qtdeleteconfirmation.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QHelpEvent>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolTip>
#include <QtTest>

class DeleteConfirmationTests final : public QObject {
    Q_OBJECT
  private slots:
    void selectionAndConsequences_data() {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("language");
        for (int mode = 0; mode < 2; ++mode)
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(qPrintable(QString("%1-%2").arg(mode).arg(language)))
                    << mode << language;
    }
    void selectionAndConsequences() {
        QFETCH(int, mode);
        QFETCH(QString, language);
        QTemporaryDir settings;
        const QString ini = settings.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), ini);
        ReloadLang(ini);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(L"-", ini);
            ReloadLang(ini);
        });
        QtDeleteConfirmation::Request request;
        request.mode = static_cast<QtDeleteConfirmation::Mode>(mode);
        request.location = "/source/каталог 空/" + QString(90, 'x');
        request.names = {QStringLiteral("длинное имя 空 ") + QString(120, 'y') + ".txt",
                         QStringLiteral("вторая папка"), QStringLiteral("third.txt")};
        request.includesFolders = true;
        QtDeleteConfirmation dialog(request);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *cancel = dialog.findChild<QPushButton *>("deleteCancel");
        auto *confirm = dialog.findChild<QPushButton *>("deleteConfirm");
        auto *source = dialog.findChild<QWidget *>("deleteSourceSummary");
        auto *folders = dialog.findChild<QLabel *>("deleteFolders");
        auto *consequence = dialog.findChild<QLabel *>("deleteConsequence");
        auto *names = dialog.findChild<QWidget *>("deleteSelectedNames");
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(cancel && confirm && source && folders && consequence && names && scroll);
        QVERIFY(cancel->hasFocus() && cancel->isDefault());
#ifdef Q_OS_MACOS
        if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
            QVERIFY(UiInspection::bounds(*cancel, dialog).right() <
                    UiInspection::bounds(*confirm, dialog).left());
#endif
        QVERIFY(confirm->isEnabled());
        QVERIFY(source->accessibleDescription().contains(request.location));
        QVERIFY(source->accessibleName().contains("3"));
        QHelpEvent hover(QEvent::ToolTip, source->rect().center(),
                         source->mapToGlobal(source->rect().center()));
        QCoreApplication::sendEvent(source, &hover);
        QCOMPARE(QToolTip::text(), request.location);
        QToolTip::hideText();
        for (auto *label : source->findChildren<QLabel *>())
            if (label->window() == dialog.window())
                QVERIFY(!label->isVisible() || label->text() != request.location);
        QVERIFY(names->isVisible());
        QStringList listedNames;
        for (auto *name : names->findChildren<QLabel *>()) {
            listedNames.append(name->text());
            QCOMPARE(name->toolTip(), name->text());
            QVERIFY(name->height() < name->fontMetrics().lineSpacing() * 2);
        }
        QCOMPARE(listedNames, request.names);
        const char *actions[] = {"Move to Trash", "Delete from archive"};
        QCOMPARE(confirm->text(), QCoreApplication::translate("DeleteRedesign", actions[mode]));
        for (const QSize size : {QSize(560, 380), QSize(340, 240)}) {
            dialog.resize(size);
            QApplication::processEvents();
            for (auto *widget : dialog.findChildren<QWidget *>()) {
                if (!widget->isVisible() || !scroll->widget()->isAncestorOf(widget))
                    continue;
                const auto reveal = [&] {
                    const QRect bounds(widget->mapTo(scroll->widget(), QPoint()), widget->size());
                    scroll->ensureVisible(bounds.left(), bounds.top(), 0, 0);
                    scroll->ensureVisible(bounds.right() + 1, bounds.bottom() + 1, 0, 0);
                    return UiInspection::fullyVisible(*widget, dialog);
                };
                if (widget->height() <= scroll->viewport()->height())
                    QTRY_VERIFY2(reveal(), qPrintable(widget->objectName()));
                if (auto *label = qobject_cast<QLabel *>(widget);
                    label && !names->isAncestorOf(label))
                    QTRY_VERIFY2(UiInspection::labelFits(*label),
                                 qPrintable(QString("%1 %2 %3x%4 needs %5")
                                                .arg(label->objectName(), label->text().left(40))
                                                .arg(label->width())
                                                .arg(label->height())
                                                .arg(label->heightForWidth(label->width()))));
                QVERIFY(UiInspection::fullyVisible(*cancel, dialog));
                QVERIFY(UiInspection::fullyVisible(*confirm, dialog));
            }
            scroll->verticalScrollBar()->setValue(0);
            QTRY_VERIFY(UiInspection::fullyVisible(*folders, dialog));
            QVERIFY(UiInspection::bounds(*folders, dialog).bottom() <
                    UiInspection::bounds(*names, dialog).top());
            QVERIFY(UiInspection::fullyVisible(*consequence, dialog));
            QVERIFY(UiInspection::bounds(*consequence, dialog).bottom() <
                    UiInspection::bounds(*names, dialog).top());
            QVERIFY(UiInspection::writeReport(
                dialog, QString("delete-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        const QString before = confirm->text();
        SaveRegLang(UString(language == "ru" ? L"-" : L"ru"), ini);
        ReloadLang(ini);
        QtNotifyLanguageChanged();
        QVERIFY(confirm->text() != before);
        confirm->setFocus();
        QTest::keyClick(confirm, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }
    void largeSelectionKeepsConfirmationCompact() {
        QtDeleteConfirmation::Request request;
        request.location = "/source";
        request.includesFolders = true;
        for (int index = 0; index < 62; ++index)
            request.names.append(QString("item-%1.txt").arg(index));
        QtDeleteConfirmation dialog(request);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QVERIFY(!dialog.isMaximized());
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        auto *cancel = dialog.findChild<QPushButton *>("deleteCancel");
        auto *confirm = dialog.findChild<QPushButton *>("deleteConfirm");
        QVERIFY(scroll && cancel && confirm);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(UiInspection::fullyVisible(*cancel, dialog));
        QVERIFY(UiInspection::fullyVisible(*confirm, dialog));
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        auto *names = dialog.findChild<QWidget *>("deleteSelectedNames");
        QVERIFY(names);
        auto *last = names->findChildren<QLabel *>().last();
        QCOMPARE(last->text(), request.names.last());
        QTRY_VERIFY(UiInspection::fullyVisible(*last, dialog));
        QVERIFY(UiInspection::writeReport(dialog, "delete-large-selection"));
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void defaultAndReadOnlyDoNotDelete() {
        for (bool readOnly : {false, true})
            for (int cancel = 0; cancel < 3; ++cancel) {
                QtDeleteConfirmation::Request request;
                request.names = {QStringLiteral("protected 空.txt")};
                request.location = "/source/";
                request.readOnly = readOnly;
                QtDeleteConfirmation dialog(request);
                QSignalSpy accepted(&dialog, &QDialog::accepted);
                dialog.show();
                QVERIFY(QTest::qWaitForWindowActive(&dialog));
                auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
                QTRY_VERIFY(scroll->viewport()->height() <=
                            scroll->widget()->heightForWidth(scroll->viewport()->width()) +
                                dialog.fontMetrics().lineSpacing() * 2);
                auto *button = dialog.findChild<QPushButton *>("deleteConfirm");
                QCOMPARE(button->isEnabled(), !readOnly);
                if (readOnly) {
                    button->click();
                    dialog.accept();
                    QVERIFY(dialog.isVisible());
                }
                if (cancel == 0)
                    QVERIFY(UiInspection::writeReport(
                        dialog, QString("delete-single-readonly-%1").arg(readOnly)));
                if (cancel == 0)
                    QTest::keyClick(&dialog, Qt::Key_Return);
                else if (cancel == 1)
                    QTest::keyClick(&dialog, Qt::Key_Escape);
                else
                    dialog.close();
                QCOMPARE(accepted.size(), 0);
                QVERIFY(!dialog.isVisible());
            }
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(DeleteConfirmationTests)
#include "delete_confirmation_tests.moc"
