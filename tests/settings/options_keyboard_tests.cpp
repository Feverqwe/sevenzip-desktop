#include "archive/operations/ZipRegistry.h"
#include "localization/LangUtils.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_page_fixture.h"
#include "support/options_presentation.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSet>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>

using OptionsPageFixture::COptionsAssociations;
using OptionsPageFixture::COptionsManager;

class OptionsKeyboardTests final : public QObject {
    Q_OBJECT
  private slots:
    void sourcePageActivation_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<Qt::KeyboardModifiers>("control");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(640, 390), QSize(520, 380)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(size.width())))
                    << language << size << Qt::KeyboardModifiers(Qt::ControlModifier);
#ifdef Q_OS_MACOS
        // Cocoa maps the physical Control key to MetaModifier by default.
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(640, 390), QSize(520, 380)})
                QTest::newRow(
                    qPrintable(QString("physical-control-%1-%2").arg(language).arg(size.width())))
                    << language << size << Qt::KeyboardModifiers(Qt::MetaModifier);
#endif
    }
    void sourcePageActivation() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QFETCH(Qt::KeyboardModifiers, control);
        QTemporaryDir fixture;
        const QString settings = fixture.filePath("activation.ini");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto associations = std::make_shared<COptionsAssociations>();
        QtFmOptionsDialog dialog(settings, nullptr, manager, associations);
        dialog.resize(size);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
        QCOMPARE(dialog.focusWidget(), dialog.findChild<QComboBox *>("appearanceChoice"));
        OptionsPresentation::show(dialog, "workSpecified");
        auto *current = dialog.findChild<QRadioButton *>("workSpecified");
        current->click();
        auto *workPath = dialog.findChild<QLineEdit *>("workPath");
        workPath->setText(fixture.path());

        OptionsPresentation::show(dialog, "appearanceChoice");
        dialog.findChild<QComboBox *>("appearanceChoice")->setFocus();
        for (int direction : {1, -1})
            for (int step = 0; step < pages->count(); ++step) {
                const int target =
                    (pages->currentIndex() + direction + pages->count()) % pages->count();
                const auto modifiers =
                    control | (direction < 0 ? Qt::ShiftModifier : Qt::NoModifier);
                QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab, modifiers);
                QCOMPARE(pages->currentIndex(), target);
                QWidget *focus = dialog.focusWidget();
                QVERIFY(focus && focus->isVisible() && focus->isEnabled());
                QVERIFY(pages->currentWidget()->isAncestorOf(focus));
                QVERIFY(current->isChecked());
                QCOMPARE(workPath->text(), fixture.path());
                QVERIFY(UiInspection::writeReport(dialog,
                                                  QString("options-section-activation-%1-%2-%3-%4")
                                                      .arg(language)
                                                      .arg(size.width())
                                                      .arg(direction)
                                                      .arg(target)));
            }
        auto *footer = dialog.findChild<QDialogButtonBox *>("settingsButtons")
                           ->button(QDialogButtonBox::Cancel);
        footer->setFocus();
        const int target = (pages->currentIndex() + 1) % pages->count();
        QTest::keyClick(footer, Qt::Key_Tab, control);
        QCOMPARE(pages->currentIndex(), target);
        QVERIFY(pages->currentWidget()->isAncestorOf(dialog.focusWidget()));
        OptionsPresentation::show(dialog, workPath);
        workPath->setFocus();
        dialog.hide();
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        QCOMPARE(dialog.focusWidget(), workPath);
        QCOMPARE(workPath->text(), fixture.path());
        dialog.reject();
        QCOMPARE(associations->writes, 0);
        QCOMPARE(associations->notifications, 0);
    }
    void integrationCapabilitiesAndKeyboardReachability_data() {
        QTest::addColumn<int>("availability");
        for (int mask = 0; mask < 4; ++mask)
            QTest::newRow(qPrintable(QString::number(mask))) << mask;
    }
    void integrationCapabilitiesAndKeyboardReachability() {
        QFETCH(int, availability);
        auto *hints = QGuiApplication::styleHints();
        const auto previousPolicy = hints->tabFocusBehavior();
        const auto restorePolicy = qScopeGuard([&] { hints->setTabFocusBehavior(previousPolicy); });
        // Test the complete Tab chain with Full Keyboard Access enabled.
        // The application's default keeps the user's native keyboard policy.
        hints->setTabFocusBehavior(Qt::TabFocusAllControls);
        QTemporaryDir fixture;
        CMyComPtr<IFolderManager> manager = new COptionsManager;
        auto services = std::make_shared<COptionsAssociations>();
        services->available[0] = availability & 1;
        services->available[1] = availability & 2;
        QtFmOptionsDialog dialog(fixture.filePath("options.ini"), nullptr, manager, services);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *choice = dialog.findChild<QCheckBox *>("association-zip");
        auto *tree = dialog.findChild<QTreeWidget *>("systemAssociations");
        QCOMPARE(choice->isEnabled(), bool(availability & 1));
        QCOMPARE(tree->isColumnHidden(2), !bool(availability & 2));
        if (availability) {
            QVERIFY(OptionsPresentation::show(dialog, choice));
            QVERIFY(!tree->isVisible());
            auto *details = dialog.findChild<QtDisclosure *>("associationDetails");
            if (availability & 2) {
                details->button()->setFocus();
                QTest::keyClick(details->button(), Qt::Key_Space);
                QVERIFY(details->isExpanded());
                QVERIFY(OptionsPresentation::show(dialog, tree));
                tree->setFocus();
                QTest::keyClick(tree, Qt::Key_Space);
                QCOMPARE(tree->topLevelItem(0)->text(1),
                         availability & 1 ? QString("7-Zip") : QString());
            } else {
                QVERIFY(!details->isVisible());
            }
        } else {
            QVERIFY(!OptionsPresentation::show(dialog, choice));
        }
        OptionsPresentation::show(dialog, "appearanceChoice");
        dialog.findChild<QComboBox *>("appearanceChoice")->setFocus();
        QSet<QWidget *> visited;
        for (int i = 0; i < 24; ++i) {
            QWidget *focus = dialog.focusWidget();
            QVERIFY(focus && focus->isVisible() && focus->isEnabled());
            if (visited.contains(focus))
                break;
            visited.insert(focus);
            QTest::keyClick(focus, Qt::Key_Tab);
        }
        QVERIFY(visited.contains(dialog.findChild<QComboBox *>("languageChoice")));
        auto *footer = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        QStringList focusTrace;
        for (auto *widget : visited)
            focusTrace.append(QString::fromLatin1(widget->metaObject()->className()) + ':' +
                              widget->objectName());
        for (auto *button : footer->buttons())
            if (button->isEnabled())
                QVERIFY2(visited.contains(button),
                         qPrintable(button->text() +
                                    QString(" policy=%1 visible=%2 focus chain: ")
                                        .arg(int(button->focusPolicy()))
                                        .arg(button->isVisible()) +
                                    focusTrace.join(',')));
        dialog.reject();
        QCOMPARE(services->writes, 0);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(OptionsKeyboardTests)
#include "options_keyboard_tests.moc"
