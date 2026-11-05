#include "dialogs/browse/BrowseDialog.h"
#include "dialogs/combo/ComboDialog.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

class SourceBrowseDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir settings_;
    QByteArray oldSettings_;
    bool hadSettings_ = false;
    static QString LongDirectory(const QTemporaryDir &fixture) {
        QString directory = fixture.path();
        while (directory.size() < 350)
            directory += QStringLiteral("/long-path-папка-with-spaces-and-another-segment");
        return directory;
    }
    static CObjectVector<CBrowseFilterInfo> Filters() {
        CObjectVector<CBrowseFilterInfo> filters;
        filters.AddNew().Description = "Archives";
        filters[0].Masks.Add(UString("*.7z"));
        filters.AddNew().Description = "All files";
        filters[1].Masks.Add(UString("*"));
        return filters;
    }
    static QDialog *Dialog() {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        return dialog && dialog->objectName() == "browseDialog" ? dialog : nullptr;
    }
    static QStringList Names(QTreeWidget &list) {
        QStringList names;
        for (int i = 0; i < list.topLevelItemCount(); ++i)
            names.append(list.topLevelItem(i)->text(0));
        return names;
    }

  private slots:
    void init() {
        QVERIFY(settings_.isValid());
        hadSettings_ = qEnvironmentVariableIsSet("SEVENZIP_SETTINGS_PATH");
        oldSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("browser.ini").toUtf8());
        CFmSettings settings;
        settings.Load(FmSettingsPath());
        settings.SingleClick = false;
        settings.ShowDots = true;
        settings.Save(FmSettingsPath());
        SaveRegLang(UString("-"), FmSettingsPath());
        ReloadLang(FmSettingsPath());
    }
    void cleanup() {
        if (hadSettings_)
            qputenv("SEVENZIP_SETTINGS_PATH", oldSettings_);
        else
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        ReloadLang(FmSettingsPath());
    }
    void sourceInitialFrame_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void sourceInitialFrame() {
        QFETCH(QString, language);
        SaveRegLang(UString(language.toStdWString().c_str()), FmSettingsPath());
        ReloadLang(FmSettingsPath());
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(UiInspection::writeReport(*dialog,
                                              QString("browse-source-initial-%1").arg(language)));
            QVERIFY(dialog->windowFlags().testFlag(Qt::WindowMinMaxButtonsHint));
            QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
            auto *list = dialog->findChild<QTreeWidget *>("browseList");
            QVERIFY(list);
            QVERIFY(UiInspection::fullyVisible(*list, *dialog));
            auto *path = dialog->findChild<QLineEdit *>("browsePath");
            QVERIFY(UiInspection::fullyVisible(*path, *dialog));
            auto *footer = dialog->findChild<QDialogButtonBox *>("browseButtons");
            const int gap = UiInspection::bounds(*footer, *dialog).top() -
                            UiInspection::bounds(*path, *dialog).bottom() - 1;
            QVERIFY(gap >= 0 && gap < path->height());
            QVERIFY(!dialog->findChild<QWidget *>("browseFilterLabel")->isVisible());
            for (const char *name : {"browseParent", "browseCreateFolder"}) {
                auto *button = dialog->findChild<QPushButton *>(name);
                QStyleOptionButton option;
                option.initFrom(button);
                option.text = button->text();
                const QRect contents =
                    button->style()->subElementRect(QStyle::SE_PushButtonContents, &option, button);
                QVERIFY2(button->iconSize().width() <= contents.width(),
                         qPrintable(QString("%1: icon %2, contents %3, button %4")
                                        .arg(button->objectName())
                                        .arg(button->iconSize().width())
                                        .arg(contents.width())
                                        .arg(button->width())));
                QVERIFY(button->iconSize().height() <= contents.height());
                QVERIFY(!button->accessibleName().isEmpty());
                QVERIFY(!button->toolTip().isEmpty());
            }
            for (auto *button : dialog->findChildren<QPushButton *>())
                QVERIFY(UiInspection::fullyVisible(*button, *dialog));
            inspected = true;
        });
        UString result;
        QVERIFY(!MyBrowseForFolder(nullptr, UString("Source browser"),
                                   UString(directory.toStdWString().c_str()), result));
        QVERIFY(inspected);
    }
    void sourceTopButtonsActivateOnEnter_data() {
        QTest::addColumn<bool>("create");
        QTest::addColumn<int>("key");
        for (bool create : {false, true})
            for (int key : {int(Qt::Key_Return), int(Qt::Key_Enter)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(create).arg(key))) << create << key;
    }
    void sourceTopButtonsActivateOnEnter() {
        QFETCH(bool, create);
        QFETCH(int, key);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(QTest::qWaitForWindowExposed(dialog));
            dialog->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(dialog));
            auto *folder = dialog->findChild<QLabel *>("browseFolder");
            const QString before = folder->toolTip();
            auto *button =
                dialog->findChild<QPushButton *>(create ? "browseCreateFolder" : "browseParent");
            button->setFocus();
            bool prompted = false;
            if (create)
                QTimer::singleShot(0, dialog, [&] {
                    auto *child = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    if (child && child != dialog) {
                        prompted = child->objectName() == "comboDialog";
                        child->reject();
                    }
                });
            QTest::keyClick(button, Qt::Key(key));
            QVERIFY(dialog->isVisible());
            if (create) {
                QVERIFY(prompted);
                QCOMPARE(folder->toolTip(), before);
            } else
                QVERIFY(folder->toolTip() != before);
            inspected = true;
        });
        UString result;
        QVERIFY(!MyBrowseForFolder(nullptr, UString("Source browser"),
                                   UString(directory.toStdWString().c_str()), result));
        QVERIFY(inspected);
    }
    void longPathUsesSourceBrowser_data() {
        QTest::addColumn<bool>("folder");
        QTest::newRow("folder") << true;
        QTest::newRow("file") << false;
    }
    void longPathUsesSourceBrowser() {
        QFETCH(bool, folder);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        const QString filePath = directory + QStringLiteral("/selected 密碼 🗜.7z");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write("selector fixture");
        file.close();
        bool sourcePresented = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            sourcePresented = dialog->objectName() == "browseDialog";
            if (!sourcePresented) {
                dialog->reject();
                return;
            }
            auto *path = dialog->findChild<QLineEdit *>("browsePath");
            if (!path) {
                dialog->reject();
                return;
            }
            path->setText(folder ? directory : filePath);
            dialog->accept();
        });
        UString output;
        bool accepted;
        if (folder)
            accepted = MyBrowseForFolder(nullptr, L"Long folder", ToUString(directory), output);
        else {
            CBrowseInfo browse;
            browse.lpstrTitle = L"Long file";
            browse.FilePath = ToUString(filePath);
            CObjectVector<CBrowseFilterInfo> filters;
            filters.AddNew().Description = "Archives";
            filters[0].Masks.Add(UString("*.7z"));
            browse.FilterIndex = 0;
            accepted = browse.BrowseForFile(filters);
            output = browse.FilePath;
        }
        QVERIFY(sourcePresented);
        QVERIFY(accepted);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(ToQString(output))),
                 folder ? directory : filePath);
    }

#ifdef Q_OS_WIN
    void superPathUsesSourceBrowser_data() {
        QTest::addColumn<bool>("folder");
        QTest::newRow("super-folder") << true;
        QTest::newRow("super-file") << false;
    }
    void superPathUsesSourceBrowser() {
        QFETCH(bool, folder);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString fileName = fixture.filePath(QStringLiteral("selected 密碼.7z"));
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write("owned selector fixture");
        file.close();
        const QString selected = QStringLiteral("\\\\?\\") +
                                 QDir::toNativeSeparators(folder ? fixture.path() : fileName);
        QVERIFY(selected.size() < 260);
        bool sourcePresented = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            sourcePresented = dialog->objectName() == "browseDialog";
            if (!sourcePresented) {
                dialog->reject();
                return;
            }
            dialog->findChild<QLineEdit *>("browsePath")->setText(selected);
            dialog->accept();
        });
        UString output;
        bool accepted;
        if (folder)
            accepted = MyBrowseForFolder(nullptr, L"Super folder", ToUString(selected), output);
        else {
            CBrowseInfo browse;
            browse.FilePath = ToUString(selected);
            browse.FilterIndex = 0;
            accepted = browse.BrowseForFile(Filters());
            output = browse.FilePath;
        }
        QVERIFY(sourcePresented);
        QVERIFY(accepted);
        QCOMPARE(ToQString(output), folder ? selected + '\\' : selected);
    }
#endif

    void filtersSortNavigationAndCreateFolder() {
        QTemporaryDir fixture;
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory + "/Z folder"));
        for (const QString &name : {"a.7z", "b.7z", "note.txt"}) {
            QFile file(directory + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            file.write(name.toUtf8());
        }
        CBrowseInfo browse;
        browse.FilePath = ToUString(directory + "/a.7z");
        browse.FilterIndex = 0;
        bool observed = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *list = dialog->findChild<QTreeWidget *>("browseList");
            auto *filter = dialog->findChild<QComboBox *>("browseFilter");
            auto *path = dialog->findChild<QLineEdit *>("browsePath");
            auto *folder = dialog->findChild<QLabel *>("browseFolder");
            QVERIFY(list && filter && path && folder);
            QCOMPARE(Names(*list), (QStringList{"..", "Z folder", "a.7z", "b.7z"}));
            auto *archive = list->findItems("a.7z", Qt::MatchExactly).front();
            QCOMPARE(archive->text(2), QString("4"));
            QVERIFY(!archive->text(1).isEmpty());
            QCOMPARE(list->findItems("Z folder", Qt::MatchExactly).front()->text(2), QString());
            filter->setCurrentIndex(1);
            QVERIFY(Names(*list).contains("note.txt"));
            filter->setCurrentIndex(0);
            QMetaObject::invokeMethod(list->header(), "sectionClicked", Qt::DirectConnection,
                                      Q_ARG(int, 0));
            QCOMPARE(Names(*list), (QStringList{"..", "Z folder", "b.7z", "a.7z"}));
            list->setFocus();
            list->setCurrentItem(list->findItems("Z folder", Qt::MatchExactly).front());
            QTest::keyClick(list, Qt::Key_Return);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(folder->toolTip())),
                     directory + "/Z folder");
            QTest::keyClick(list, Qt::Key_Backspace);
            QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(folder->toolTip())), directory);
            QTimer::singleShot(0, [&] {
                auto *create = qobject_cast<CComboDialog *>(QApplication::activeModalWidget());
                if (!create)
                    return;
                create->findChild<QComboBox *>("comboValue")->setCurrentText("created 密碼");
                create->accept();
            });
            QTest::keyClick(list, Qt::Key_F7);
            QVERIFY(QDir(directory + "/created 密碼").exists());
            QVERIFY(Names(*list).contains("created 密碼"));
            // A refresh retains the current name through enumeration and sort.
            QTest::keyClick(list, Qt::Key_R, Qt::ControlModifier);
            QCOMPARE(list->selectedItems().front()->text(0), QString("created 密碼"));
            path->setText("a.7z");
            path->setFocus();
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            observed = true;
        });
        QVERIFY(browse.BrowseForFile(Filters()));
        QVERIFY(observed);
        QCOMPARE(QDir::fromNativeSeparators(ToQString(browse.FilePath)), directory + "/a.7z");
        QCOMPARE(browse.FilterIndex, 0);
    }

    void cancelPreservesRawFileStateAndClearsFolderOutput() {
        QTemporaryDir fixture;
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        CBrowseInfo browse;
        const UString original = ToUString(directory + QStringLiteral("/  密碼 🗜.7z  "));
        browse.FilePath = original;
        browse.FilterIndex = 1;
        QTimer::singleShot(0, [] {
            if (auto *dialog = Dialog()) {
                dialog->findChild<QLineEdit *>("browsePath")->setText("edited");
                dialog->findChild<QComboBox *>("browseFilter")->setCurrentIndex(0);
                dialog->reject();
            }
        });
        QVERIFY(!browse.BrowseForFile(Filters()));
        QCOMPARE(ToQString(browse.FilePath), ToQString(original));
        QCOMPARE(browse.FilterIndex, 1);
        UString output(L"stale");
        QTimer::singleShot(0, [] {
            if (auto *dialog = Dialog())
                dialog->reject();
        });
        QVERIFY(!MyBrowseForFolder(nullptr, L"Folder", ToUString(directory), output));
        QVERIFY(output.IsEmpty());
    }

    void saveModePreservesSpacesInNewName() {
        QTemporaryDir fixture;
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        CBrowseInfo browse;
        browse.SaveMode = true;
        browse.FilePath = ToUString(directory + "/initial.7z");
        browse.FilterIndex = 0;
        const QString name = QStringLiteral("  new 密碼 🗜.7z  ");
        QTimer::singleShot(0, [&] {
            if (auto *dialog = Dialog()) {
                QCOMPARE(dialog->findChild<QDialogButtonBox *>("browseButtons")
                             ->button(QDialogButtonBox::Ok)
                             ->text(),
                         QCoreApplication::translate("DialogRedesign", "Save"));
                dialog->findChild<QLineEdit *>("browsePath")->setText(name);
                dialog->accept();
            }
        });
        QVERIFY(browse.BrowseForFile(Filters()));
        QCOMPARE(QDir::fromNativeSeparators(ToQString(browse.FilePath)), directory + '/' + name);
        QVERIFY(!QFile::exists(ToQString(browse.FilePath)));
    }

    void malformedPathCanBeCorrectedOrCancelled_data() {
        QTest::addColumn<bool>("folder");
        QTest::addColumn<bool>("cancel");
        for (bool folder : {false, true})
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(folder).arg(cancel)))
                    << folder << cancel;
    }
    void malformedPathCanBeCorrectedOrCancelled() {
        QFETCH(bool, folder);
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = LongDirectory(fixture);
        const QString name =
            folder ? QStringLiteral("selected folder 空") : QStringLiteral("selected file 空.7z");
        QVERIFY(QDir().mkpath(directory + '/' + (folder ? name : QString())));
        const auto original = ToUString(directory + "/initial.7z");
        CBrowseInfo browse;
        browse.SaveMode = true;
        browse.FilePath = original;
        browse.FilterIndex = 0;
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *path = dialog->findChild<QLineEdit *>("browsePath");
            auto *list = dialog->findChild<QTreeWidget *>("browseList");
            const QStringList before = Names(*list);
            auto *filter = dialog->findChild<QComboBox *>("browseFilter");
            if (!folder)
                filter->setCurrentIndex(1);
            const QString invalid = QString("shortened") + QChar::Null + "suffix";
            path->setText(invalid);
            bool reported = false;
            QTimer::singleShot(0, dialog, [&] {
                if (auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    reported = error->icon() == QMessageBox::Critical && !error->text().isEmpty();
                    error->accept();
                }
            });
            path->setFocus();
            dialog->findChild<QDialogButtonBox *>("browseButtons")
                ->button(QDialogButtonBox::Ok)
                ->click();
            QVERIFY(reported);
            QVERIFY(dialog->isVisible());
            QCOMPARE(path->text(), invalid);
            QCOMPARE(Names(*list), before);
            inspected = true;
            if (cancel)
                dialog->reject();
            else {
                path->setText(directory + '/' + name);
                dialog->accept();
            }
        });
        UString output(L"stale");
        const bool accepted =
            folder ? MyBrowseForFolder(nullptr, L"Folder", ToUString(directory), output)
                   : browse.BrowseForFile(Filters());
        QVERIFY(inspected);
        QCOMPARE(accepted, !cancel);
        if (folder)
            QCOMPARE(QDir::fromNativeSeparators(ToQString(output)),
                     cancel ? QString() : directory + '/' + name + '/');
        else {
            QCOMPARE(QDir::fromNativeSeparators(ToQString(browse.FilePath)),
                     cancel ? ToQString(original) : directory + '/' + name);
            QCOMPARE(browse.FilterIndex, cancel ? 0 : 1);
        }
    }

    void createFolderCancelAndFailureKeepBrowserOpen() {
        QTemporaryDir fixture;
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        QFile file(directory + "/occupied");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write("unchanged");
        file.close();
        CBrowseInfo browse;
        browse.FilePath = ToUString(directory + "/initial.7z");
        browse.FilterIndex = 1;
        bool observed = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            auto *list = dialog->findChild<QTreeWidget *>("browseList");
            const QStringList before = Names(*list);
            QTimer::singleShot(0, [] {
                if (auto *create = qobject_cast<CComboDialog *>(QApplication::activeModalWidget()))
                    create->reject();
            });
            auto *createButton = dialog->findChild<QPushButton *>("browseCreateFolder");
            QVERIFY(createButton);
            createButton->setFocus();
            createButton->click();
            QCOMPARE(dialog->focusWidget(), static_cast<QWidget *>(list));
            QCOMPARE(Names(*list), before);
            bool errorShown = false;
            QTimer::singleShot(0, [&] {
                auto *create = qobject_cast<CComboDialog *>(QApplication::activeModalWidget());
                if (!create)
                    return;
                create->findChild<QComboBox *>("comboValue")->setCurrentText("occupied");
                QTimer::singleShot(0, [&] {
                    if (auto *error =
                            qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                        errorShown =
                            error->icon() == QMessageBox::Critical && !error->text().isEmpty();
                        error->accept();
                    }
                });
                create->accept();
            });
            QTest::keyClick(list, Qt::Key_F7);
            QVERIFY(errorShown);
            QVERIFY(dialog->isVisible());
            QCOMPARE(Names(*list), before);
            observed = true;
        });
        QVERIFY(!browse.BrowseForFile(Filters()));
        QVERIFY(observed);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("unchanged"));
    }

    void geometry_data() {
        QTest::addColumn<bool>("folder");
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("language");
        for (const QString &language : {QString("-"), QString("ru")})
            for (int width : {560, 420})
                for (bool folder : {true, false})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(language).arg(width).arg(folder)))
                        << folder << width << language;
    }
    void geometry() {
        QFETCH(bool, folder);
        QFETCH(int, width);
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), FmSettingsPath());
        ReloadLang(FmSettingsPath());
        QTemporaryDir fixture;
        const QString directory = LongDirectory(fixture);
        QVERIFY(QDir().mkpath(directory));
        bool observed = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = Dialog();
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] {
                if (dialog->isVisible())
                    dialog->reject();
            });
            dialog->resize(width, width == 560 ? 640 : 400);
            QApplication::processEvents();
            auto *list = dialog->findChild<QTreeWidget *>("browseList");
            auto *path = dialog->findChild<QLineEdit *>("browsePath");
            auto *filter = dialog->findChild<QComboBox *>("browseFilter");
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            auto *currentFolder = dialog->findChild<QLabel *>("browseFolder");
            QVERIFY(list && path && filter && buttons && currentFolder);
            QCOMPARE(
                buttons->button(QDialogButtonBox::Ok)->text(),
                QCoreApplication::translate("DialogRedesign", folder ? "Choose folder" : "Open"));
            QVERIFY(UiInspection::fullyVisible(*currentFolder, *dialog));
            QVERIFY(UiInspection::labelFits(*currentFolder));
            QCOMPARE(currentFolder->accessibleName(), currentFolder->toolTip());
            QVERIFY(currentFolder->text().contains(QChar(0x2026)));
            for (QWidget *widget : QList<QWidget *>{list, path, buttons})
                QVERIFY(UiInspection::fullyVisible(*widget, *dialog));
            for (const char *name : {"browsePathLabel", "browseFilterLabel"}) {
                auto *label = dialog->findChild<QLabel *>(name);
                if (label->isVisible()) {
                    QVERIFY(UiInspection::fullyVisible(*label, *dialog));
                    QVERIFY(UiInspection::labelFits(*label));
                }
            }
            QVERIFY(UiInspection::bounds(*list, *dialog).bottom() <
                    UiInspection::bounds(*path, *dialog).top());
            QCOMPARE(filter->isVisible(), !folder);
            if (!folder) {
                QVERIFY(UiInspection::fullyVisible(*filter, *dialog));
                QVERIFY(UiInspection::separate(*path, *filter, *dialog));
            }
            QVERIFY(UiInspection::separate(*path, *buttons, *dialog));
            QVERIFY(UiInspection::separate(*buttons->button(QDialogButtonBox::Ok),
                                           *buttons->button(QDialogButtonBox::Cancel), *dialog));
            QVERIFY(UiInspection::writeReport(
                *dialog, QString("source-browse-%1").arg(QTest::currentDataTag())));
            observed = true;
        });
        UString output;
        if (folder)
            QVERIFY(!MyBrowseForFolder(nullptr, L"Folder", ToUString(directory), output));
        else {
            CBrowseInfo browse;
            browse.FilePath = ToUString(directory + "/new.7z");
            browse.FilterIndex = 0;
            QVERIFY(!browse.BrowseForFile(Filters()));
        }
        QVERIFY(observed);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceBrowseDialogTests)
#include "source_browse_dialog_tests.moc"
