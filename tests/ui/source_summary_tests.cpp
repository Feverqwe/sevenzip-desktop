#include "ui/controls/qtsourcesummary.h"
#include <QHelpEvent>
#include <QLabel>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtTest>

class SourceSummaryTests final : public QObject {
    Q_OBJECT
  private slots:
    void longPathsStayInHoverWithoutDisplacingTheIdentity() {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        const QString name = QStringLiteral("данные & <file>.txt");
        const QString path = '/' + QStringLiteral("long folder 空/").repeated(80) + name;
        auto *shortSource = new QtSourceSummary({}, name, "/short/" + name, &window);
        auto *longSource = new QtSourceSummary({}, name, path, &window);
        layout->addWidget(shortSource);
        layout->addWidget(longSource);
        shortSource->SetDescription("File inside archive");
        longSource->SetDescription("File inside archive");
        window.resize(360, 200);
        window.show();
        QApplication::processEvents();
        QCOMPARE(longSource->height(), shortSource->height());
        QCOMPARE(longSource->heightForWidth(320), shortSource->heightForWidth(320));
        bool nameVisible = false, typeVisible = false;
        for (auto *label : longSource->findChildren<QLabel *>()) {
            if (!label->isVisible())
                continue;
            nameVisible |= label->text() == name;
            typeVisible |= label->text() == "File inside archive";
            QVERIFY(label->text() != path);
        }
        QVERIFY(nameVisible && typeVisible);
        const auto hover = [&](const QString &expected) {
            QHelpEvent event(QEvent::ToolTip, longSource->rect().center(),
                             longSource->mapToGlobal(longSource->rect().center()));
            QCoreApplication::sendEvent(longSource, &event);
            QCOMPARE(QToolTip::text(), expected);
            QToolTip::hideText();
        };
        hover(path);
        longSource->SetSource("another.txt", "/new source/another.txt");
        longSource->SetDescription(QStringLiteral("Файл внутри архива"));
        hover("/new source/another.txt");
        QVERIFY(longSource->accessibleDescription().contains(QStringLiteral("Файл внутри архива")));
        QVERIFY(longSource->accessibleDescription().contains("/new source/another.txt"));
        QVERIFY(!longSource->accessibleDescription().contains(path));
    }
};

QTEST_MAIN(SourceSummaryTests)
#include "source_summary_tests.moc"
