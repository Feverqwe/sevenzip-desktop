#include "dialogs/copy/CopyDialog.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialogunits.h"
#include "ui/support/layout_inspection.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <initializer_list>

class CopyDialogTests : public QObject {
    Q_OBJECT

    static UStringVector SourceStrings(std::initializer_list<const wchar_t *> values) {
        UStringVector strings;
        for (const wchar_t *value : values)
            strings.Add(value);
        return strings;
    }
    static QStringList QtStrings(const UStringVector &values) {
        QStringList strings;
        FOR_VECTOR(i, values)
        strings.append(ToQString(values[i]));
        return strings;
    }
    static void inspectSourceFrame(CCopyDialog &dialog, const QString &tag) {
        QVERIFY(UiInspection::writeReport(dialog, tag));
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        auto *browse = dialog.findChild<QPushButton *>("copyBrowseButton");
        auto *source = dialog.findChild<QWidget *>("copySourceSummary");
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("copyButtons");
        auto *label = dialog.findChild<QLabel *>("copyDestinationLabel");
        QVERIFY(path && browse && source && name && scroll && buttons && label);
        for (QWidget *widget : QList<QWidget *>{source, label, path, browse, name}) {
            QApplication::processEvents();
            const QRect rect(widget->mapTo(scroll->widget(), QPoint()), widget->size());
            scroll->ensureVisible(rect.left(), rect.top(), 0, 0);
            QVERIFY(widget->mapTo(scroll->viewport(), QPoint()).y() >= 0);
            scroll->ensureVisible(rect.right() + 1, rect.bottom() + 1, 0, 0);
            const QRect visible(widget->mapTo(scroll->viewport(), QPoint()), widget->size());
            QVERIFY(visible.bottom() < scroll->viewport()->height());
            QVERIFY2(visible.left() >= 0 && visible.right() < scroll->viewport()->width(),
                     qPrintable(widget->objectName()));
            if (widget->height() <= scroll->viewport()->height())
                QVERIFY(UiInspection::fullyVisible(*widget, dialog));
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        }
        QVERIFY(UiInspection::separate(*path, *browse, dialog));
        QVERIFY(UiInspection::bounds(*label, dialog).bottom() <
                UiInspection::bounds(*path, dialog).top());
        QVERIFY(UiInspection::bounds(*source, dialog).bottom() <
                UiInspection::bounds(*path, dialog).top());
    }

  private slots:
    void init() {
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }

    void sourceFrameAndKeyboard_data() {
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("russian");
        QTest::addColumn<int>("points");
        for (bool move : {false, true})
            for (bool russian : {false, true})
                for (int points : {9, 12, 32})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(move).arg(russian).arg(points)))
                        << move << russian << points;
    }
    void sourceFrameAndKeyboard() {
        QFETCH(bool, move);
        QFETCH(bool, russian);
        QFETCH(int, points);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QFont original = QApplication::font();
        const auto restore = qScopeGuard([&] {
            QApplication::setFont(original);
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        });
        QFont font = original;
        font.setPointSize(points);
        QApplication::setFont(font);
        const QString settings = fixture.filePath("language.ini");
        SaveRegLang(UString(russian ? L"ru" : L"-"), settings);
        ReloadLang(settings);
        const QString initial = QStringLiteral("/destination space 空/");
        const QString sourceName = QStringLiteral("имя с пробелом 空.txt");
        CCopyDialog dialog(move, initial, {"/source/" + sourceName}, {"/one", "/two"});
        dialog.ConfigureDestination(
            {"/source/", sourceName, {}, false, [](const QString &) { return true; }});
        QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMinimizeButtonHint));
        QVERIFY(dialog.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
        QVERIFY(!dialog.windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        const QSize work = dialog.screen()->availableGeometry().size();
        QTRY_VERIFY(dialog.width() <= work.width());
        QTRY_VERIFY(dialog.height() <= work.height());
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        auto *browse = dialog.findChild<QPushButton *>("copyBrowseButton");
        auto *name = dialog.findChild<QLineEdit *>("copyDestinationName");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("copyButtons");
        QVERIFY(path && browse && name && buttons);
        QVERIFY(path->hasFocus() || path->lineEdit()->hasFocus());
        QCOMPARE(path->lineEdit()->selectedText(), initial);
        QCOMPARE(name->text(), sourceName);
        QVERIFY(UiInspection::fullyVisible(*name, dialog));
        QVERIFY(UiInspection::fullyVisible(*path, dialog));
        const QString tag = QString("copy-source-frame-%1").arg(QTest::currentDataTag());
        dialog.setProperty("uiInspection_language", russian ? "ru" : "en");
        dialog.setProperty("uiInspection_state", move ? "move-initial" : "copy-initial");
        inspectSourceFrame(dialog, tag + "-initial");
        dialog.showNormal();
        dialog.resize(340, 220);
        QApplication::processEvents();
        dialog.setProperty("uiInspection_state", move ? "move-compact" : "copy-compact");
        inspectSourceFrame(dialog, tag + "-compact");
        const QSet<QWidget *> allowed{path,
                                      path->lineEdit(),
                                      browse,
                                      name,
                                      buttons->button(QDialogButtonBox::Ok),
                                      buttons->button(QDialogButtonBox::Cancel)};
        for (int i = 0; i < 5; ++i) {
            QTest::keyClick(dialog.focusWidget(), Qt::Key_Tab);
            QVERIFY(allowed.contains(dialog.focusWidget()));
        }
        path->lineEdit()->setFocus();
        const QString changed = QStringLiteral("  /changed space 空/  ");
        path->setCurrentText(changed);
        QTest::keyClick(path->lineEdit(), Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ToQString(dialog.Value), changed);
        CCopyDialog cancelled(move, initial, {"/source/" + sourceName}, {});
        cancelled.ConfigureDestination(
            {"/source/", sourceName, {}, false, [](const QString &) { return true; }});
        cancelled.show();
        QVERIFY(QTest::qWaitForWindowActive(&cancelled));
        cancelled.findChild<QComboBox *>("copyDestination")->setCurrentText(changed);
        QTest::keyClick(cancelled.focusWidget(), Qt::Key_Escape);
        QCOMPARE(cancelled.result(), int(QDialog::Rejected));
        QCOMPARE(ToQString(cancelled.Value), initial);
    }

    void copiesFieldsAndCommitsOnlyOnOK() {
        CCopyDialog dialog(false, QStringLiteral("/начальная папка/"),
                           {QStringLiteral("/tmp/имя с пробелом.txt")}, {"/one", "/two"});
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        QVERIFY(path);
        QCOMPARE(path->count(), 2);
        QCOMPARE(path->itemText(1), QString("/two"));
        auto *source = dialog.findChild<QWidget *>("copySourceSummary");
        QVERIFY(source);
        QCOMPARE(source->accessibleName(), QStringLiteral("имя с пробелом.txt"));
        path->setCurrentText(QStringLiteral("  /новая папка/  "));
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("/начальная папка/"));
        dialog.accept();
        QCOMPARE(ToQString(dialog.Value), QStringLiteral("  /новая папка/  "));
        CCopyDialog move(true, "/initial", {"/source/file"}, {});
        move.findChild<QComboBox *>("copyDestination")->setCurrentText("/by-button");
        move.findChild<QDialogButtonBox *>("copyButtons")->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(ToQString(move.Value), QString("/by-button"));
        QCOMPARE(move.result(), int(QDialog::Accepted));
    }

    void historyRemovesAllCaseVariantsAndIgnoresMvpINI() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString ini = temp.filePath(QStringLiteral("настройки с пробелом.ini"));
        {
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue("copy/history", QStringList{"/old", "/OLDER"});
            settings.setValue("unrelated/value", 17);
        }
        UStringVector history = SourceStrings({L"stale"});
        ReadCopyHistory(history, ini);
        QVERIFY(history.IsEmpty());
        SaveCopyHistory(SourceStrings({L"/old", L"/OLDER"}), ini);
        ReadCopyHistory(history, ini);
        QCOMPARE(QtStrings(history), (QStringList{"/old", "/OLDER"}));
        history.Add(L"/NEW");
        history.Add(L"/new");
        history.Add(L"/NeW");
        AddUniqueStringToHeadOfList(history, L"/new");
        QCOMPARE(QtStrings(history), (QStringList{"/new", "/old", "/OLDER"}));
        UStringVector unicode = SourceStrings({L"/ПАПКА", L"/keep", L"/папка"});
        AddUniqueStringToHeadOfList(unicode, L"/Папка");
        QCOMPARE(QtStrings(unicode), (QStringList{QStringLiteral("/Папка"), "/keep"}));
        UStringVector sourceCase = SourceStrings({L"/\u212A", L"/k", L"/K", L"/keep"});
        AddUniqueStringToHeadOfList(sourceCase, L"/K");
        QCOMPARE(QtStrings(sourceCase), (QStringList{"/K", QStringLiteral("/\u212A"), "/keep"}));
        SaveCopyHistory(sourceCase, ini);
        UStringVector restarted;
        ReadCopyHistory(restarted, ini);
        QCOMPARE(QtStrings(restarted), QtStrings(sourceCase));
        CCopyDialog sourceHistory(false, "/destination", {}, QtStrings(restarted));
        auto *destinations = sourceHistory.findChild<QComboBox *>("copyDestination");
        QCOMPARE(destinations->count(), 3);
        QCOMPARE(destinations->itemText(1), QStringLiteral("/\u212A"));
        SaveCopyHistory(history, ini);
        history.Clear();
        ReadCopyHistory(history, ini);
        QCOMPARE(QtStrings(history), (QStringList{"/new", "/old", "/OLDER"}));
        QSettings settings(ini, QSettings::IniFormat);
        QCOMPARE(settings.value("unrelated/value").toInt(), 17);
        QCOMPARE(settings.value("copy/history").toStringList(), (QStringList{"/old", "/OLDER"}));
        SaveCopyHistory({}, ini);
        ReadCopyHistory(history, ini);
        QVERIFY(history.IsEmpty());
        ReadCopyHistory(history, temp.filePath("missing.ini"));
        QVERIFY(history.IsEmpty());
    }

    void browseNormalizesDirectoryAndCancelPreservesText() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString chosen = temp.filePath(QStringLiteral("папка назначения"));
        QVERIFY(QDir().mkdir(chosen));
        CCopyDialog dialog(false, temp.path(), {temp.filePath("source")}, {chosen});
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        auto *browse = dialog.findChild<QPushButton *>("copyBrowseButton");
        dialog.show();
        bool found = false;
        QTimer::singleShot(0, [&found] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *picker = qobject_cast<QFileDialog *>(widget);
                    picker && picker->isVisible()) {
                    found = true;
                    picker->reject();
                }
            }
        });
        browse->click();
        QVERIFY(found);
        QCOMPARE(path->currentText(), temp.path());
        found = false;
        QTimer::singleShot(0, [&found, &chosen] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *picker = qobject_cast<QFileDialog *>(widget);
                    picker && picker->isVisible()) {
                    found = true;
                    picker->setDirectory(chosen);
                    QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
                }
            }
        });
        browse->click();
        QVERIFY(found);
        QCOMPARE(path->currentIndex(), -1);
        QCOMPARE(QDir::fromNativeSeparators(path->currentText()), chosen + '/');
        dialog.accept();
        QCOMPARE(QDir::fromNativeSeparators(ToQString(dialog.Value)), chosen + '/');
    }

    void languageChangePreservesDestinationAndDisclosure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto restore = qScopeGuard([&] {
            ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
            QtNotifyLanguageChanged();
        });
        CCopyDialog dialog(false, "/initial/", {"/source/one", "/source/two"}, {});
        dialog.show();
        auto *source = dialog.findChild<QWidget *>("copySourceSummary");
        auto *selection = dialog.findChild<QtDisclosure *>("copySelectionDisclosure");
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        QVERIFY(source && selection && path);
        const QString original = source->accessibleName();
        const QString draft = QStringLiteral(" /новое назначение/ ");
        path->setCurrentText(draft);
        selection->setExpanded(true);
        const QString settings = fixture.filePath("language.ini");
        SaveRegLang(UString(L"ru"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        QVERIFY(source->accessibleName() != original);
        QVERIFY(source->accessibleName().contains('2'));
        QVERIFY(selection->isExpanded());
        QCOMPARE(path->currentText(), draft);
        dialog.reject();
        QCOMPARE(ToQString(dialog.Value), QString("/initial/"));
    }

    void geometryAndLongSelection_data() {
        QTest::addColumn<bool>("move");
        QTest::addColumn<QSize>("size");
        for (bool move : {false, true}) {
            QTest::newRow(move ? "move-normal" : "copy-normal") << move << QSize(440, 250);
            QTest::newRow(move ? "move-compact" : "copy-compact") << move << QSize(340, 220);
        }
    }

    void geometryAndLongSelection() {
        QFETCH(bool, move);
        QFETCH(QSize, size);
        QStringList sources;
        CObjectVector<CPanelItem> items;
        CRecordVector<UInt32> indices;
        for (int i = 0; i < 40; ++i) {
            sources << QStringLiteral("/tmp/очень длинное имя с пробелами %1.txt").arg(i);
            indices.Add(items.Add({QFileInfo(sources.last()).fileName(), i == 0, true, 1000}));
        }
        const QString summary = CPanel("/initial/", items).GetItemsInfoString(indices);
        CCopyDialog dialog(move, "/initial", sources, {"/one"}, nullptr, summary);
        dialog.SetSourceDetails("/tmp/", 40000);
        dialog.resize(size);
        dialog.show();
        QCoreApplication::processEvents();
        auto *label = dialog.findChild<QLabel *>("copyDestinationLabel");
        auto *path = dialog.findChild<QComboBox *>("copyDestination");
        auto *browse = dialog.findChild<QPushButton *>("copyBrowseButton");
        auto *source = dialog.findChild<QWidget *>("copySourceSummary");
        auto *selection = dialog.findChild<QtDisclosure *>("copySelectionDisclosure");
        auto *names = dialog.findChild<QPlainTextEdit *>("copySelectedNames");
        auto *buttons = dialog.findChild<QDialogButtonBox *>("copyButtons");
        QVERIFY(label && path && browse && source && selection && names && buttons);
        QVERIFY(!selection->isExpanded());
        QVERIFY(!names->isVisible());
        auto *scroll = dialog.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        for (QWidget *widget : QList<QWidget *>{source, label, path, browse}) {
            QApplication::processEvents();
            const QPoint center = widget->mapTo(scroll->widget(), widget->rect().center());
            scroll->ensureVisible(center.x(), center.y(), (widget->width() + 2) / 2,
                                  (widget->height() + 2) / 2);
            QVERIFY2(UiInspection::fullyVisible(*widget, dialog), qPrintable(widget->objectName()));
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        }
        QVERIFY(UiInspection::labelFits(*label));
        QVERIFY(UiInspection::separate(*path, *browse, dialog));
        QVERIFY(UiInspection::bounds(*label, dialog).bottom() <
                UiInspection::bounds(*path, dialog).top());
        QVERIFY(UiInspection::bounds(*source, dialog).bottom() <
                UiInspection::bounds(*path, dialog).top());
        QVERIFY(UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                       *buttons->button(QDialogButtonBox::Cancel), dialog));
        QVERIFY(source->accessibleDescription().contains('\n'));
        selection->button()->click();
        QCoreApplication::processEvents();
        QVERIFY(names->isVisible());
        QCOMPARE(names->focusPolicy(), Qt::NoFocus);
        QVERIFY(names->toPlainText().contains(sources.last()));
        names->moveCursor(QTextCursor::End);
        QCOMPARE(names->textCursor().blockNumber(), sources.size() - 1);
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        selection->button()->click();
        dialog.SetSourceDetails("/tmp/", std::nullopt);
        QCOMPARE(source->accessibleDescription(), QString("/tmp/"));
        QVERIFY(UiInspection::writeReport(
            dialog, QString("copy-dialog-%1-%2").arg(move).arg(size.width())));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(CopyDialogTests)
#include "copy_dialog_tests.moc"
