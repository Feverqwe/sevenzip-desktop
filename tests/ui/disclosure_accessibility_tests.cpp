#include "ui/controls/qtdisclosure.h"
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

class DisclosureAccessibilityTests final : public QObject {
    Q_OBJECT
  private slots:
    void assistiveActionExposesStateAndRetainsDraft_data() {
        QTest::addColumn<QString>("action");
        QTest::newRow("press") << QAccessibleActionInterface::pressAction();
        QTest::newRow("expand-collapse-bridge") << QAccessibleActionInterface::showMenuAction();
    }
    void assistiveActionExposesStateAndRetainsDraft() {
        QFETCH(QString, action);
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *draft = new QLineEdit(QStringLiteral("черновик 密碼"));
        auto *section = new QtDisclosure(draft, &window);
        section->button()->setText("Advanced options");
        layout->addWidget(section);
        section->setSummary(QStringLiteral("Изменено"));
        window.show();
        QCoreApplication::processEvents();
        auto *accessible = QAccessible::queryAccessibleInterface(section->button());
        QVERIFY(accessible);
        QVERIFY(!accessible->text(QAccessible::Name).isEmpty());
        QVERIFY(accessible->state().expandable);
        QVERIFY(accessible->state().collapsed);
        QVERIFY(!accessible->state().expanded);
        QVERIFY(!draft->isVisible());
        auto *summary = section->findChild<QLabel *>("disclosureSummary");
        QVERIFY(summary->isVisible());
        const int headerTop = section->button()->mapTo(&window, QPoint()).y();
        auto *actions = accessible->actionInterface();
        QVERIFY(actions);
        const QString press = QAccessibleActionInterface::pressAction();
        QVERIFY(actions->actionNames().contains(press));
        actions->doAction(action);
        QVERIFY(draft->isVisible());
        QVERIFY(!summary->isVisible());
        QCOMPARE(section->button()->mapTo(&window, QPoint()).y(), headerTop);
        QVERIFY(accessible->state().expanded);
        QVERIFY(!accessible->state().collapsed);
        draft->setText(QStringLiteral("  изменённый черновик  "));
        actions->doAction(action);
        QVERIFY(!draft->isVisible());
        QVERIFY(summary->isVisible());
        QVERIFY(accessible->state().collapsed);
        section->setExpanded(true);
        QVERIFY(accessible->state().expanded);
        QCOMPARE(draft->text(), QStringLiteral("  изменённый черновик  "));
        section->button()->setText(QStringLiteral("Дополнительные параметры"));
        QCOMPARE(accessible->text(QAccessible::Name), section->button()->text());
        section->button()->setAccessibleName(QStringLiteral("Параметры архива"));
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Параметры архива"));
        section->button()->setFocus();
        QTest::keyClick(section->button(), Qt::Key_Space);
        QVERIFY(accessible->state().collapsed);
        QVERIFY(!draft->isVisible());
        QTest::keyClick(section->button(), Qt::Key_Space);
        QVERIFY(accessible->state().expanded);
        QCOMPARE(draft->text(), QStringLiteral("  изменённый черновик  "));
        section->button()->setEnabled(false);
        QVERIFY(accessible->state().disabled);
        actions->doAction(action);
        QVERIFY(draft->isVisible());
    }
};
QTEST_MAIN(DisclosureAccessibilityTests)
#include "disclosure_accessibility_tests.moc"
