#include "localization/qtlangbuttonbox.h"
#include "support/window_activation.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/support/layout_inspection.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QtTest>

class DialogSurfaceTests final : public QObject {
    Q_OBJECT
  private slots:
    void themeSwitchPreservesAnActiveDraft() {
        auto *hints = QApplication::styleHints();
        const auto original = hints->colorScheme();
        const auto originalPalette = QApplication::palette();
        const auto restore = qScopeGuard([&] {
            hints->setColorScheme(original);
            QApplication::setPalette(originalPalette);
        });
        const auto changeTheme = [&](Qt::ColorScheme scheme) {
            hints->setColorScheme(scheme);
            QCoreApplication::processEvents();
            // Offscreen has no system color-scheme provider. Exercise the
            // same application's palette fallback used on such platforms.
            if (hints->colorScheme() == Qt::ColorScheme::Unknown) {
                auto palette = QApplication::palette();
                palette.setColor(QPalette::Window,
                                 scheme == Qt::ColorScheme::Dark ? Qt::black : Qt::white);
                QApplication::setPalette(palette);
                QCoreApplication::processEvents();
            }
        };
        changeTheme(Qt::ColorScheme::Light);
        auto nativePalette = QApplication::palette();
        const auto accent = nativePalette.color(QPalette::Highlight);
        QLinearGradient selectionBrush(0, 0, 100, 0);
        selectionBrush.setColorAt(0, accent.darker(110));
        selectionBrush.setColorAt(1, accent.lighter(110));
        nativePalette.setBrush(QPalette::Button, selectionBrush);
        QApplication::setPalette(nativePalette);
        int commits = 0;
        QWidget host;
        auto inherited = QApplication::palette();
        const auto nativeHighlight = inherited.brush(QPalette::Highlight);
        inherited.setColor(QPalette::Highlight, Qt::red);
        host.setPalette(inherited);
        host.show();
        QDialog dialog(&host);
        dialog.setAttribute(Qt::WA_WindowPropagation);
        QCOMPARE(dialog.palette().brush(QPalette::Highlight),
                 host.palette().brush(QPalette::Highlight));
        auto *actions = new QtLangButtonBox(QDialogButtonBox::Close, &dialog);
        auto *content = QtCreateDialogLayout(dialog, *actions);
        auto *field = new QLineEdit(&dialog);
        field->setEchoMode(QLineEdit::Password);
        content->addWidget(field);
        QDialog peer(&dialog);
        auto *peerActions = new QtLangButtonBox(QDialogButtonBox::Close, &peer);
        auto *peerContent = QtCreateDialogLayout(peer, *peerActions, QtDialogContent::Scrollable);
        peerContent->addWidget(new QLineEdit("Other draft", &peer));
        peer.show();
        dialog.show();
        QVERIFY(WindowActivation::waitForActive(dialog));
        field->setFocus();
        QTRY_VERIFY(field->hasFocus());
        QTest::keyClicks(field, "  untouched draft  ");
        field->setSelection(2, 5);
        const auto value = field->text();
        const auto selection = field->selectedText();
        const int cursor = field->cursorPosition();
        connect(field, &QLineEdit::editingFinished, &dialog, [&] { ++commits; });
        const auto light = dialog.grab().toImage().pixelColor(4, 4);
        QCOMPARE(peer.palette().color(QPalette::Window), dialog.palette().color(QPalette::Window));
        QVERIFY(field->palette().color(QPalette::Base) != dialog.palette().color(QPalette::Window));
        QCOMPARE(field->palette().brush(QPalette::Highlight), nativeHighlight);
        QCOMPARE(actions->button(QDialogButtonBox::Close)->palette().brush(QPalette::Button),
                 QApplication::palette().brush(QPalette::Button));
        changeTheme(Qt::ColorScheme::Dark);
        QTRY_VERIFY(dialog.grab().toImage().pixelColor(4, 4).lightness() < light.lightness());
        QCOMPARE(peer.palette().color(QPalette::Window), dialog.palette().color(QPalette::Window));
        QCOMPARE(field->palette().brush(QPalette::Highlight),
                 QApplication::palette().brush(QPalette::Highlight));
        QCOMPARE(field->text(), value);
        QCOMPARE(field->selectedText(), selection);
        QCOMPARE(field->cursorPosition(), cursor);
        QCOMPARE(field->echoMode(), QLineEdit::Password);
        QVERIFY(field->hasFocus());
        QCOMPARE(commits, 0);
        changeTheme(Qt::ColorScheme::Light);
        QTRY_COMPARE(dialog.grab().toImage().pixelColor(4, 4), light);
        QCOMPARE(field->text(), value);
        QCOMPARE(field->selectedText(), selection);
        QCOMPARE(commits, 0);
    }
    void compactSurfaceKeepsActionsAndKeyboardReachable_data() {
        QTest::addColumn<int>("points");
        QTest::newRow("normal") << 12;
        QTest::newRow("large") << 24;
    }
    void compactSurfaceKeepsActionsAndKeyboardReachable() {
        QFETCH(int, points);
        const QFont original = QApplication::font();
        const auto restore = qScopeGuard([&] { QApplication::setFont(original); });
        QFont font = original;
        font.setPointSize(points);
        QApplication::setFont(font);
        QDialog dialog;
        dialog.setWindowTitle("Component surface");
        auto *buttons =
            new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto *content = QtCreateDialogLayout(dialog, *buttons, QtDialogContent::Scrollable);
        QList<QLineEdit *> fields;
        for (int index = 0; index < 6; ++index) {
            auto *label = new QtWrappingLabel(QString("Field %1").arg(index + 1), &dialog);
            label->setWordWrap(true);
            auto *field = new QLineEdit(QString("  значение %1 空  ").arg(index), &dialog);
            label->setBuddy(field);
            content->addWidget(label);
            content->addWidget(field);
            fields << field;
        }
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(360, 240);
        dialog.show();
        QVERIFY(WindowActivation::waitForActive(dialog));
        QTRY_COMPARE(dialog.size(), QSize(360, 240));
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        for (auto *button : buttons->buttons())
            QVERIFY(button->width() >= button->fontMetrics().horizontalAdvance(button->text()));
        const QRect footer = UiInspection::bounds(*buttons, dialog);
        fields.front()->setFocus();
        for (int index = 1; index < fields.size(); ++index)
            QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab);
        QCOMPARE(dialog.focusWidget(), fields.back());
        QTRY_VERIFY(UiInspection::fullyVisible(*fields.back(), dialog));
        QVERIFY(scroll->verticalScrollBar()->value() > 0);
        QCOMPARE(UiInspection::bounds(*buttons, dialog), footer);
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        for (int index = 0; index < fields.size(); ++index)
            QCOMPARE(fields[index]->text(), QString("  значение %1 空  ").arg(index));
        QVERIFY(UiInspection::writeReport(dialog, QString("dialog-surface-%1-bottom").arg(points)));
        QTest::keyClick(fields.back(), Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }
    void flexibleChildShrinksBeforeTheFormScrolls() {
        QDialog dialog;
        auto *buttons =
            new QtLangButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        auto *content = QtCreateDialogLayout(dialog, *buttons, QtDialogContent::Scrollable);
        auto *caption = new QtWrappingLabel("Read-only selection", &dialog);
        caption->setWordWrap(true);
        auto *details =
            new QPlainTextEdit(QString("A complete selection line\n").repeated(80), &dialog);
        details->setReadOnly(true);
        details->setMinimumHeight(details->fontMetrics().lineSpacing() * 3);
        content->addWidget(caption);
        content->addWidget(details, 1);
        dialog.resize(400, 240);
        dialog.show();
        QVERIFY(WindowActivation::waitForActive(dialog));
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        QTRY_VERIFY(details->verticalScrollBar()->maximum() > 0);
        QCOMPARE(scroll->verticalScrollBar()->maximum(), 0);
        QVERIFY(UiInspection::fullyVisible(*details, dialog));
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(DialogSurfaceTests)
#include "dialog_surface_tests.moc"
