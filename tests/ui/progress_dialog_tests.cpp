#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/FileManager/resource.h"
#include "7zip/UI/FileManager/resourceGui.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

namespace {
QString text(const CProgressDialog &dialog, const char *name) {
    return dialog.findChild<QLabel *>(name)->text();
}
QPushButton *pause(CProgressDialog &dialog) { return dialog.findChild<QPushButton *>("IDB_PAUSE"); }
QTreeWidget *messages(CProgressDialog &dialog) {
    return dialog.findChild<QTreeWidget *>("IDL_PROGRESS_MESSAGES");
}
} // namespace

class ProgressDialogTests : public QObject {
    Q_OBJECT

  private slots:
    void sourceIconIDAndEmbeddedFrames() {
        CProgressDialog normal;
        const auto originalIcon = normal.windowIcon().cacheKey();
        normal.show();
        QCOMPARE(normal.windowIcon().cacheKey(), originalIcon);
        CProgressDialog source;
        source.IconID = IDI_ICON;
        source.show();
        QVERIFY(!source.windowIcon().isNull());
        for (const int size : {16, 32, 48}) {
            QVERIFY(source.windowIcon().availableSizes().contains(QSize(size, size)));
            // Inspect the embedded 1x frame; the default overload can return
            // a Retina pixmap with twice the requested logical dimensions.
            const QPixmap frame = source.windowIcon().pixmap(QSize(size, size), 1.0);
            QVERIFY(!frame.isNull());
            QCOMPARE(frame.size(), QSize(size, size));
        }
        CProgressDialog invalid;
        invalid.IconID = 99999;
        invalid.show();
        QVERIFY(invalid.windowIcon().isNull());
        CProgressDialog second;
        second.IconID = IDI_ICON;
        second.show();
        QVERIFY(!second.windowIcon().isNull());
    }
    void initialUnknownStateAndSourceCommands() {
        CProgressDialog dialog(nullptr, L"Обновление");
        QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
        dialog.show();
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        QTRY_COMPARE(bar->minimum(), bar->maximum());
        QCOMPARE(text(dialog, "progressPercent"), QString());
        dialog.Sync.Set_NumBytesTotal(100);
        dialog.Sync.Set_NumBytesCur(62);
        QTRY_COMPARE(text(dialog, "progressPercent"), QString("62%"));
        dialog.Sync.Set_NumBytesTotal(0);
        QTRY_COMPARE(text(dialog, "progressPercent"), QString());
        QTRY_COMPARE(bar->minimum(), bar->maximum());
        QVERIFY(!messages(dialog)->isVisible());
        QVERIFY(!dialog.findChild<QPushButton *>("IDB_PROGRESS_BACKGROUND"));
        QVERIFY(dialog.findChild<QPushButton *>("IDCANCEL")->isEnabled());
        QCOMPARE(dialog.focusWidget(), dialog.findChild<QPushButton *>("IDCANCEL"));
        QVERIFY(pause(dialog)->isEnabled());
        dialog.accept();
        QTimer::singleShot(0, &dialog, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->button(QMessageBox::No)->click();
        });
        dialog.reject();
        QVERIFY(dialog.isVisible());
        QVERIFY(!dialog.Sync.Get_Stopped());
    }
    void countersRatioTitleAndUnicodePath() {
        QWidget main;
        main.setWindowTitle("7-Zip");
        {
            CProgressDialog dialog(nullptr, L"Обновление");
            dialog.MainWindow = &main;
            dialog.MainTitle = L"7-Zip";
            dialog.MainAddTitle = L"Архив ";
            dialog.Sync.Set_NumBytesTotal(4000);
            QCOMPARE(dialog.Sync.Set_NumBytesCur(2000), S_OK);
            QCOMPARE(dialog.Sync.Set_NumFilesTotal(4), S_OK);
            dialog.Sync.Set_NumFilesCur(2);
            UInt64 in = 2000, out = 500;
            dialog.Sync.Set_Ratio(&in, &out);
            dialog.Sync.Set_TitleFileName(L"архив с пробелом.7z");
            dialog.Sync.Set_Status(L"Добавление");
            dialog.Sync.Set_FilePath(L"/папка с пробелом/файл.txt");
            dialog.show();
            QTRY_COMPARE(text(dialog, "progressPercent"), QString("50%"));
            QCOMPARE(text(dialog, "IDT_PROGRESS_STATUS"), QStringLiteral("Добавление"));
            QCOMPARE(text(dialog, "IDT_PROGRESS_FILE_NAME"), QStringLiteral("файл.txt"));
            QCOMPARE(dialog.findChild<QLabel *>("IDT_PROGRESS_FILE_NAME")->accessibleDescription(),
                     QStringLiteral("/папка с пробелом/файл.txt"));
            QCOMPARE(dialog.windowTitle(), QStringLiteral("50% Обновление архив с пробелом.7z"));
            QCOMPARE(main.windowTitle(), QStringLiteral("50% Архив 7-Zip"));
            QVERIFY(UiInspection::writeReport(dialog, "progress-counters"));
            dialog.Sync.Set_FilePath(L"/папка с пробелом/", true);
            QTRY_COMPARE(text(dialog, "IDT_PROGRESS_FILE_NAME"),
                         QStringLiteral("/папка с пробелом/"));
            dialog.Sync.Set_NumBytesTotal(~UInt64(0));
            QTRY_COMPARE(text(dialog, "progressPercent"), QString());
            QTRY_VERIFY(!dialog.windowTitle().contains('%'));
        }
        QCOMPARE(main.windowTitle(), QString("7-Zip"));
    }
    void decompressingAndFilesProgress() {
        CProgressDialog dialog;
        dialog.CompressingMode = false;
        dialog.Sync.Set_FilesProgressMode(true);
        QCOMPARE(dialog.Sync.Set_NumFilesTotal(8), S_OK);
        dialog.Sync.Set_NumFilesCur(2);
        dialog.Sync.Set_NumBytesTotal(1000);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(750), S_OK);
        UInt64 in = 100, out = 400;
        dialog.Sync.Set_Ratio(&in, &out);
        dialog.show();
        QTRY_VERIFY(dialog.windowTitle().startsWith("25% "));
        QCOMPARE(text(dialog, "progressPercent"), QString("25%"));
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        QCOMPARE(bar->maximum(), 9);
        QCOMPARE(bar->value(), 2);
    }
    void sourceRangeConverterAndBackwardProgress_data() {
        QTest::addColumn<quint64>("total");
        QTest::newRow("small") << quint64(32767);
        QTest::newRow("larger") << quint64(32768);
        QTest::newRow("large-64-bit") << quint64(1ULL << 63);
        QTest::newRow("largest-defined") << quint64(~UInt64(0) - 1);
    }
    void sourceRangeConverterAndBackwardProgress() {
        QFETCH(quint64, total);
        CProgressDialog dialog;
        dialog.Sync.Set_NumBytesTotal(total);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(total / 2), S_OK);
        dialog.show();
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        QTRY_VERIFY(bar->maximum() > 0);
        QTRY_VERIFY(qAbs(double(bar->value()) / bar->maximum() - 0.5) < 0.001);
        QTRY_VERIFY(qAbs(text(dialog, "progressPercent").remove('%').toInt() - 50) <= 1);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(total), S_OK);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QCOMPARE(dialog.Sync.Set_NumBytesCur(UInt64(0)), S_OK);
        QTRY_COMPARE(bar->value(), 0);
    }
    void pauseStopsRenderingAndPreservesElapsedTime() {
        CProgressDialog dialog(nullptr, L"Обновление");
        dialog.Sync.Set_NumBytesTotal(100);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(25), S_OK);
        dialog.show();
        QTRY_VERIFY(dialog.windowTitle().startsWith("25% "));
        const auto position = dialog.findChild<QProgressBar *>("IDC_PROGRESS1")->value();
        pause(dialog)->click();
        QVERIFY(dialog.Sync.Get_Paused());
        QCOMPARE(pause(dialog)->text(), QStringLiteral("&Continue"));
        QVERIFY(dialog.windowTitle().startsWith(QStringLiteral("Paused 25% ")));
        dialog.Sync.Set_Status(L"Пока приостановлено");
        // Counter setters that wait on pause belong to the worker; status does not wait.
        QTest::qWait(1100);
        QCOMPARE(text(dialog, "IDT_PROGRESS_STATUS"), QString());
        QCOMPARE(dialog.findChild<QProgressBar *>("IDC_PROGRESS1")->value(), position);
        pause(dialog)->click();
        QVERIFY(!dialog.Sync.Get_Paused());
        QCOMPARE(pause(dialog)->text(), QStringLiteral("&Pause"));
        QTRY_COMPARE(text(dialog, "IDT_PROGRESS_STATUS"), QStringLiteral("Пока приостановлено"));
    }
    void nativeProgressControlClampsOutOfRangePosition() {
        CProgressDialog dialog;
        dialog.Sync.Set_NumBytesTotal(100);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(25), S_OK);
        dialog.show();
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        QTRY_COMPARE(bar->value(), 25);
        QCOMPARE(dialog.Sync.Set_NumBytesCur(150), S_OK);
        QTRY_COMPARE(bar->value(), bar->maximum());
        QCOMPARE(dialog.Sync.Set_NumBytesCur(UInt64(0)), S_OK);
        QTRY_COMPARE(bar->value(), 0);
    }
    void errorLinesAppendNumberingAndClipboard() {
        CProgressDialog dialog;
        dialog.Sync.AddError_Message_Name(L"Ошибка\nПодробность", L"файл с пробелом.txt");
        dialog.Sync.AddError_Message(L"");
        dialog.show();
        auto *list = messages(dialog);
        QTRY_COMPARE(list->topLevelItemCount(), 4);
        auto *disclosure =
            dynamic_cast<QtDisclosure *>(dialog.findChild<QWidget *>("progressDiagnostics"));
        QVERIFY(disclosure);
        disclosure->setExpanded(true);
        QVERIFY(list->isVisible());
        QVERIFY(list->isColumnHidden(0));
        QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("файл с пробелом.txt"));
        QCOMPARE(list->topLevelItem(2)->text(1), QStringLiteral("Подробность"));
        list->setFocus();
        list->clearSelection();
        const QString newline =
#ifdef _WIN32
            "\r\n";
#else
            "\n";
#endif
        QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(),
                 QStringLiteral("файл с пробелом.txt") + newline + QStringLiteral("Ошибка") +
                     newline + QStringLiteral("Подробность") + newline + newline);
        list->topLevelItem(1)->setSelected(true);
        QTest::keyClick(list, Qt::Key_Insert, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Ошибка") + newline);
        QTest::keyClick(list, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(list->selectedItems().size(), 4);
        dialog.Sync.AddError_Message(L"Ещё ошибка\n");
        QTRY_COMPARE(list->topLevelItemCount(), 6);
        QCOMPARE(list->topLevelItem(5)->text(1), QString());
    }
    void initialSourceAndDestinationFitWithoutScrolling_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void initialSourceAndDestinationFitWithoutScrolling() {
        QFETCH(QString, language);
        QTemporaryDir settings;
        const QString path = settings.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), path);
        ReloadLang(path);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(L"-", path);
            ReloadLang(path);
        });
        CProgressDialog dialog(nullptr, LangString(IDS_COPYING));
        dialog.SetLocations(QStringLiteral("/source 空"),
                            QStringLiteral("/destination with spaces"));
        dialog.Sync.Set_FilePath(L"/source 空/current.txt");
        dialog.Sync.Set_NumBytesTotal(100);
        dialog.Sync.Set_NumBytesCur(62);
        dialog.show();
        QCOMPARE(text(dialog, "IDT_PROGRESS_FILE_NAME"), QString("current.txt"));
        QCOMPARE(text(dialog, "progressPercent"), QString("62%"));
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QApplication::processEvents();
        for (const char *name : {"progressPercent", "progressLocations", "progressButtons"})
            QTRY_VERIFY2(UiInspection::fullyVisible(*dialog.findChild<QWidget *>(name), dialog),
                         name);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QVERIFY(UiInspection::writeReport(dialog, "progress-initial-" + language));
        dialog.done(0);
    }
    void sourceLayoutStates_data() {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<bool>("errors");
        QTest::addColumn<QString>("language");
        for (const int width : {560, 360})
            for (const bool errors : {false, true})
                for (const QString &language : {QString("-"), QString("ru")})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(width).arg(errors).arg(language)))
                        << QSize(width, width == 560 ? 300 : 240) << errors << language;
    }
    void sourceLayoutStates() {
        QFETCH(QSize, size);
        QFETCH(bool, errors);
        QFETCH(QString, language);
        QTemporaryDir settings;
        const QString path = settings.filePath("language.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), path);
        ReloadLang(path);
        const auto restore = qScopeGuard([&] {
            SaveRegLang(L"-", path);
            ReloadLang(path);
        });
        CProgressDialog dialog(nullptr, LangString(IDS_COPYING));
        dialog.SetLocations(QStringLiteral("/источник 空/") + QString(90, 'x'),
                            QStringLiteral("/назначение 空/") + QString(90, 'y'));
        dialog.Sync.Set_FilePath(L"/источник 空/файл.txt");
        dialog.Sync.Set_NumBytesTotal(100);
        dialog.Sync.Set_NumBytesCur(62);
        if (errors)
            dialog.Sync.AddError_Message(L"Ошибка файла с пробелом");
        dialog.resize(size);
        dialog.show();
        QCOMPARE(text(dialog, "IDT_PROGRESS_FILE_NAME"), QStringLiteral("файл.txt"));
        if (errors)
            QTRY_VERIFY(dialog.findChild<QWidget *>("progressDiagnostics")->isVisible());
        QApplication::processEvents();
        QCOMPARE(dialog.size(), size.expandedTo(dialog.minimumSizeHint()));
        const QString tag = QString("progress-dialog-%1-%2%3")
                                .arg(size.width())
                                .arg(errors)
                                .arg(language == "ru" ? "-ru" : "");
        QVERIFY(UiInspection::writeReport(dialog, tag + "-collapsed"));
        auto *locations =
            dynamic_cast<QtDisclosure *>(dialog.findChild<QWidget *>("progressLocations"));
        auto *diagnostics =
            dynamic_cast<QtDisclosure *>(dialog.findChild<QWidget *>("progressDiagnostics"));
        locations->setExpanded(true);
        if (errors)
            diagnostics->setExpanded(true);
        QApplication::processEvents();
        QVERIFY(UiInspection::writeReport(dialog, tag));
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        for (QWidget *widget : dialog.findChildren<QWidget *>()) {
            if (!widget->isVisible() || (widget->objectName().isEmpty()))
                continue;
            if (!qobject_cast<const QLabel *>(widget) && !qobject_cast<const QPushButton *>(widget))
                continue;
            if (scroll->widget()->isAncestorOf(widget))
                scroll->ensureWidgetVisible(widget);
            QApplication::processEvents();
            if (widget->height() <= scroll->viewport()->height())
                QTRY_VERIFY2(UiInspection::fullyVisible(*widget, dialog),
                             qPrintable(widget->objectName()));
            if (scroll->isAncestorOf(widget) && scroll->verticalScrollBar()->isVisible())
                QVERIFY2(UiInspection::separate(*widget, *scroll->verticalScrollBar(), dialog),
                         qPrintable(widget->objectName()));
            if (qobject_cast<const QLabel *>(widget) &&
                !text(dialog, qPrintable(widget->objectName())).isEmpty())
                QVERIFY2(UiInspection::labelFits(*widget), qPrintable(widget->objectName()));
            if (qobject_cast<const QPushButton *>(widget)) {
                QVERIFY2(widget->sizeHint().width() <= widget->width(),
                         qPrintable(widget->objectName()));
                QVERIFY2(widget->sizeHint().height() <= widget->height(),
                         qPrintable(widget->objectName()));
            }
        }
        auto *bar = dialog.findChild<QProgressBar *>("IDC_PROGRESS1");
        auto *file = dialog.findChild<QLabel *>("IDT_PROGRESS_FILE_NAME");
        QVERIFY(UiInspection::bounds(*file, dialog).bottom() <
                UiInspection::bounds(*bar, dialog).top());
        auto *cancel = dialog.findChild<QPushButton *>("IDCANCEL");
        QVERIFY(pause(dialog)->geometry().right() < cancel->geometry().left());
        QVERIFY(UiInspection::fullyVisible(*cancel, dialog));
        QVERIFY(UiInspection::fullyVisible(*pause(dialog), dialog));
        QVERIFY(UiInspection::writeReport(dialog, tag));
        pause(dialog)->click();
        QCOMPARE(pause(dialog)->text(), QString::fromWCharArray(LangString(IDS_CONTINUE)));
        QTRY_VERIFY(pause(dialog)->sizeHint().width() <= pause(dialog)->width());
        QVERIFY(dialog.windowTitle().startsWith(
            QString::fromWCharArray(LangString(IDS_PROGRESS_PAUSED))));
        QVERIFY(UiInspection::writeReport(dialog, tag + "-paused"));
        locations->setExpanded(false);
        pause(dialog)->click();
        dialog.Sync.Set_NumBytesTotal(2048);
        dialog.Sync.Set_NumBytesCur(1024);
        QTRY_COMPARE(text(dialog, "progressPercent"), QString("50%"));
        locations->setExpanded(true);
        QVERIFY(dialog.findChild<QLabel *>("progressSource")->text().contains(QString(90, 'x')));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ProgressDialogTests)
#include "progress_dialog_tests.moc"
