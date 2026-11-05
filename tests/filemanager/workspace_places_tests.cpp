#include "filemanager/window/qtfavoritestore.h"
#include "filemanager/window/qtworkspacesidebar.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QListWidget>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

class WorkspacePlacesTests final : public QObject {
    Q_OBJECT
  private slots:
    void compactPlacesRemainReachable_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QStringLiteral("-");
        QTest::newRow("ru") << QStringLiteral("ru");
    }
    void compactPlacesRemainReachable() {
        QFETCH(QString, language);
        QTemporaryDir fixture;
        const auto settings = fixture.filePath("places.ini");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        QtFavoriteStore favorites(settings);
        for (int index = 0; index < 8; ++index)
            favorites.Add(fixture.filePath(QString::number(index)),
                          QString("Очень длинное имя избранного — Project %1").arg(index));
        QString opened;
        bool addInvoked = false;
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *sidebar = new QtPlacesSidebar(favorites,
                                            {[&] { addInvoked = true; },
                                             [&](const QString &path, QtPlaceTarget target) {
                                                 QCOMPARE(target, QtPlaceTarget::Current);
                                                 opened = path;
                                             }},
                                            0, &window);
        sidebar->SetCompact(true);
        sidebar->SetCurrentLocation(fixture.path());
        layout->addWidget(sidebar);
        window.resize(140, 230);
        window.show();
        auto *add = sidebar->findChild<QToolButton *>("addFavoriteButton");
        auto *locations = sidebar->findChild<QListWidget *>("nativePlaces");
        QVERIFY(add && locations && locations->count());
        QTRY_VERIFY(UiInspection::fullyVisible(*add, window));
        QTest::mouseClick(add, Qt::LeftButton);
        QVERIFY(addInvoked);
        QTRY_VERIFY(sidebar->verticalScrollBar()->maximum() > 0);
        sidebar->verticalScrollBar()->setValue(sidebar->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const auto last = locations->model()->index(locations->count() - 1, 0);
        const QRect row = locations->visualRect(last);
        const QRect exposed(locations->viewport()->mapTo(sidebar->viewport(), row.topLeft()),
                            row.size());
        QVERIFY(sidebar->viewport()->rect().contains(exposed));
        QTest::mouseClick(locations->viewport(), Qt::LeftButton, Qt::NoModifier, row.center());
        QCOMPARE(opened, last.data(Qt::UserRole + 1).toString());
        QVERIFY(!opened.isEmpty());
        QVERIFY(
            UiInspection::writeReport(window, QString("workspace-places-%1-bottom").arg(language)));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(WorkspacePlacesTests)
#include "workspace_places_tests.moc"
