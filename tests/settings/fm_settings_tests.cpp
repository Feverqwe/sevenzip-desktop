#include "settings/SettingsPage.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class FmSettingsTests : public QObject {
    Q_OBJECT
  private slots:
    void defaultsAndInvalidValues() {
        QTemporaryDir dir;
        const QString path = dir.filePath("настройки списка.ini");
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue("FM/ShowGrid", "corrupt");
        ini.setValue("list/singleClick", true);
        ini.setValue("FM/SingleClick", 1);
        ini.setValue("FM/FullRow", 2);
        ini.sync();
        CFmSettings st;
        st.Load(path);
        QVERIFY(!st.ShowDots && !st.ShowRealFileIcons && !st.ShowGrid);
        QVERIFY(st.FullRow);
        QVERIFY(st.SingleClick);
        QVERIFY(!st.AlternativeSelection);
        st.ShowDots = st.FullRow = st.AlternativeSelection = true;
        st.Save(path);
        CFmSettings loaded;
        loaded.Load(path);
        QVERIFY(loaded.ShowDots && loaded.FullRow && loaded.AlternativeSelection &&
                loaded.SingleClick);
        QVERIFY(!loaded.ShowGrid);
    }
    void confirmCancelAndPersistListFlags() {
        QTemporaryDir dir;
        const QString path = dir.filePath("fm.ini");
        CFmSettings st;
        st.Load(path);
        st.Save(path);
        for (bool confirm : {false, true}) {
            CSettingsPage page(path);
            for (const char *name :
                 {"showDotsOption", "realFileIconsOption", "fullRowOption", "gridOption",
                  "singleClickOption", "alternativeSelectionOption"})
                page.findChild<QCheckBox *>(name)->click();
            QVERIFY(page.findChild<QCheckBox *>("alternativeSelectionOption")->isEnabled());
            if (confirm)
                page.accept();
            else
                page.reject();
            st.Load(path);
            QCOMPARE(st.ShowDots, confirm);
            QCOMPARE(st.ShowRealFileIcons, confirm);
            QCOMPARE(st.FullRow, !confirm);
            QCOMPARE(st.ShowGrid, confirm);
            QCOMPARE(st.SingleClick, confirm);
            QCOMPARE(st.AlternativeSelection, confirm);
        }
    }
    void geometry() {
        QTemporaryDir dir;
        for (int width : {460, 340}) {
            CSettingsPage page(dir.filePath("fm.ini"));
            page.resize(width, 390);
            page.show();
            QApplication::processEvents();
            auto *buttons = page.findChild<QDialogButtonBox *>("settingsButtons");
            QWidget *previous = nullptr;
            for (const char *name :
                 {"showDotsOption", "realFileIconsOption", "fullRowOption", "gridOption",
                  "singleClickOption", "alternativeSelectionOption", "extractMemoryLabel",
                  "extractMemorySpin"}) {
                auto *control = page.findChild<QWidget *>(name);
                QVERIFY(control);
                QVERIFY(UiInspection::fullyVisible(*control, page));
                if (qobject_cast<QCheckBox *>(control))
                    QVERIFY(control->width() >= control->sizeHint().width());
                else
                    QVERIFY(control->width() >= control->minimumSizeHint().width());
                if (auto *label = qobject_cast<QLabel *>(control); label && label->wordWrap())
                    QVERIFY(UiInspection::labelFits(*label));
                else
                    QVERIFY(control->height() >= control->sizeHint().height());
                if (previous)
                    QVERIFY(UiInspection::bounds(*previous, page).bottom() <
                            UiInspection::bounds(*control, page).top());
                previous = control;
            }
            QVERIFY(UiInspection::fullyVisible(*buttons, page));
            QVERIFY(UiInspection::bounds(*previous, page).bottom() <
                    UiInspection::bounds(*buttons, page).top());
            QVERIFY(UiInspection::writeReport(page, QString("settings-%1").arg(width)));
        }
    }
};
QTEST_MAIN(FmSettingsTests)
#include "fm_settings_tests.moc"
