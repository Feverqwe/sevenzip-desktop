#include "ui/controls/qtwindowstatusbar.h"
#include <QHelpEvent>
#include <QLabel>
#include <QTextDocument>
#include <QToolTip>
#include <QtTest>

class WindowStatusTests final : public QObject {
    Q_OBJECT
  private slots:
    void compactSummaryElidesWithFullTooltipAndKnownSize() {
        QObject source;
        QtWindowStatusBar bar;
        bar.resize(220, 36);
        const QString full = QStringLiteral("Left · Selected objects: ").repeated(15);
        bar.SetSummary(&source, full, "16 EB", "18,446,744,073,709,551,615 bytes", false);
        bar.show();
        QApplication::processEvents();
        auto *summary = bar.findChild<QLabel *>("selectedItemsStatus");
        auto *size = bar.findChild<QLabel *>("selectedSizeStatus");
        QVERIFY(summary->width() < summary->fontMetrics().horizontalAdvance(full));
        QVERIFY(size->isVisible() && size->width() >= size->sizeHint().width());
        QHelpEvent help(QEvent::ToolTip, summary->rect().center(),
                        summary->mapToGlobal(summary->rect().center()));
        QCoreApplication::sendEvent(summary, &help);
        QCOMPARE(QToolTip::text(), full);
        QToolTip::hideText();
    }

    void messagesExpireToCurrentContextAndSelection() {
        QObject left, right;
        QtWindowStatusBar bar;
        bar.resize(500, 40);
        bar.show();
        bar.SetSummary(&left, "Left selection", "7 bytes", "7 bytes", false);
        auto *size = bar.findChild<QLabel *>("selectedSizeStatus");
        auto *summary = bar.findChild<QLabel *>("selectedItemsStatus");
        QVERIFY(size->isVisible());
        const QString message = QStringLiteral("Cannot open:\n") +
                                QStringLiteral("/long  folder").repeated(20) + "/<b>&.zip";
        bar.SetMessage(&left, message);
        QCOMPARE(bar.currentMessage(), message);
        QVERIFY(!size->isVisible());
        QHelpEvent help(QEvent::ToolTip, bar.rect().center(), bar.mapToGlobal(bar.rect().center()));
        QCoreApplication::sendEvent(&bar, &help);
        QTextDocument tooltip;
        tooltip.setHtml(QToolTip::text());
        QCOMPARE(tooltip.toPlainText(), message);
        QToolTip::hideText();
        bar.SetSummary(&right, "Right selection", "12 bytes", "12 bytes", false);
        QVERIFY(bar.currentMessage().isEmpty());
        QVERIFY(bar.toolTip().isEmpty());
        QVERIFY(size->isVisible());
        // An inactive source cannot replace the current strip.
        bar.SetMessage(&left, "Left operation", 100);
        QVERIFY(bar.currentMessage().isEmpty());
        bar.SetMessage(&right, "Right operation", 100);
        bar.SetSummary(&right, "New selection", "20 bytes", "20 bytes", false);
        QTRY_VERIFY(bar.currentMessage().isEmpty());
        QVERIFY(bar.toolTip().isEmpty());
        QCOMPARE(summary->text(), QString("New selection"));
        QCOMPARE(size->text(), QString("20 bytes"));
        QVERIFY(size->isVisible());
        bar.SetSummary(&left, "Left current selection", "9 bytes", "9 bytes", false);
        QVERIFY(bar.currentMessage().isEmpty());
        QCOMPARE(summary->text(), QString("Left current selection"));
    }

    void loadingAndDestroyedSourceCannotExposeOldSize() {
        auto source = std::make_unique<QObject>();
        QtWindowStatusBar bar;
        bar.show();
        bar.SetSummary(source.get(), "Selection", "1 MB", "1,048,576 bytes", false);
        bar.SetMessage(source.get(), "Old operation");
        bar.SetSummary(source.get(), "Reading folder…", {}, {}, true);
        QVERIFY(bar.currentMessage().isEmpty());
        QVERIFY(!bar.findChild<QLabel *>("selectedSizeStatus")->isVisible());
        bar.SetMessage(source.get(), {});
        bar.SetSummary(source.get(), "Empty folder", {}, {}, false);
        QVERIFY(!bar.findChild<QLabel *>("selectedSizeStatus")->isVisible());
        source.reset();
        QVERIFY(bar.findChild<QLabel *>("selectedItemsStatus")->text().isEmpty());
    }
};

QTEST_MAIN(WindowStatusTests)
#include "window_status_tests.moc"
