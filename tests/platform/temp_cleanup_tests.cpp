#include "7zip/UI/FileManager/PropertyNameRes.h"
#include "7zip/UI/FileManager/resource.h"
#include "dialogs/browse/BrowseDialog2.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "platform/support/native_link_fixture.h"
#include "platform/temp/qtownedtempdir.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QStyleOptionButton>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

class TempCleanupTests : public QObject {
    Q_OBJECT
    QUrl opened;
    QTemporaryDir registry_;
    QTemporaryDir settings_;
    QByteArray oldRegistry_;
    bool hadRegistry_ = false;
    bool privateSettings_ = false;
    static QTreeWidgetItem *row(CBrowseDialog2 &dialog, const QString &path) {
        auto *list = dialog.findChild<QTreeWidget *>("tempList");
        const QString name = QFileInfo(path).fileName().normalized(QString::NormalizationForm_C);
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            if (list->topLevelItem(i)->text(0).normalized(QString::NormalizationForm_C) == name)
                return list->topLevelItem(i);
        return nullptr;
    }
    static void select(CBrowseDialog2 &dialog, const QString &path) {
        auto *list = dialog.findChild<QTreeWidget *>("tempList");
        list->clearSelection();
        if (auto *item = row(dialog, path)) {
            list->setCurrentItem(item);
            item->setSelected(true);
        }
    }
    static bool file(const QString &path) {
        QFile output(path);
        return output.open(QIODevice::WriteOnly) && output.write("fixture") == 7;
    }
    static bool inactive(const QString &path) {
        auto lease = QtOwnedTempRegistry().RegisterCreated(path);
        return bool(lease);
    }
  public slots:
    void captureFile(const QUrl &url) { opened = url; }
  private slots:
    void initTestCase() {
        QVERIFY(registry_.isValid());
        hadRegistry_ = qEnvironmentVariableIsSet("SEVENZIP_OWNED_TEMP_REGISTRY");
        oldRegistry_ = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", registry_.path().toUtf8());
        if (!qEnvironmentVariableIsSet("SEVENZIP_SETTINGS_PATH")) {
            QVERIFY(settings_.isValid());
            qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
            privateSettings_ = true;
        }
    }
    void cleanupTestCase() {
        if (hadRegistry_)
            qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", oldRegistry_);
        else
            qunsetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        if (privateSettings_)
            qunsetenv("SEVENZIP_SETTINGS_PATH");
    }
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void failedNavigationRetainsListingAndRootCommands() {
#ifdef Q_OS_WIN
        QSKIP("This fixture requires POSIX directory read permissions");
#else
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        const QString sentinel = owned.filePath(QStringLiteral("keep 文件.txt"));
        QVERIFY(file(sentinel));
        QVERIFY(inactive(owned.path()));
        const auto permissions = QFileInfo(owned.path()).permissions();
        const auto restore = qScopeGuard([&] { QFile::setPermissions(owned.path(), permissions); });
        QVERIFY(
            QFile::setPermissions(owned.path(), QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY(!QFileInfo(owned.path()).isReadable());
        bool denied = false, oldListing = false, rootBlocked = false, recovered = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            auto *path = dialog->findChild<QLineEdit *>("tempPath");
            const QString previousPath = path->text();
            QStringList previousRows;
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                previousRows.append(list->topLevelItem(i)->text(0));
            QTimer answers;
            connect(&answers, &QTimer::timeout, dialog, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    denied = true;
                    box->accept();
                }
            });
            answers.start(1);
            select(*dialog, owned.path());
            QVERIFY2(row(*dialog, owned.path()), qPrintable(owned.path()));
            QTest::keyClick(list, Qt::Key_Return);
            QStringList remainingRows;
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                remainingRows.append(list->topLevelItem(i)->text(0));
            oldListing = path->text() == previousPath && remainingRows == previousRows;
            const auto outsideCommands = [&](bool enabled) {
                bool found = false, matched = true;
                QTimer::singleShot(0, dialog, [&] {
                    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                    QVERIFY(menu);
                    QString label = QtLangText(IDM_OPEN_OUTSIDE, "Open Outside");
                    label.remove('&');
                    for (auto *action : menu->actions())
                        if (action->text().startsWith(label)) {
                            found = true;
                            matched &= action->isEnabled() == enabled;
                        }
                    menu->close();
                });
                list->clearSelection();
                QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, QPoint(10, 5)));
                return found && matched;
            };
            rootBlocked = outsideCommands(false);
            QVERIFY(QFile::setPermissions(owned.path(), permissions));
            select(*dialog, owned.path());
            QTest::keyClick(list, Qt::Key_Return);
            recovered = QDir::cleanPath(path->text()) == QDir::cleanPath(owned.path()) &&
                        row(*dialog, sentinel) && outsideCommands(true);
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(denied);
        QVERIFY(oldListing);
        QVERIFY(rootBlocked);
        QVERIFY(recovered);
        QFile bytes(sentinel);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        QCOMPARE(bytes.readAll(), QByteArray("fixture"));
#endif
    }
    void liveLanguageChangePreservesLinkSelection() {
        QTemporaryDir owned, outside;
        QVERIFY(owned.isValid() && outside.isValid());
        const QString link = owned.filePath("translated link");
        QVERIFY(CreateNativeSymbolicLink(link, outside.path()));
        QVERIFY(inactive(owned.path()));
        bool observed = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            select(*dialog, owned.path());
            QTest::keyClick(list, Qt::Key_Return);
            select(*dialog, link);
            auto *item = row(*dialog, link);
            QVERIFY(item && item->isSelected());
            const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
            for (const char *code : {"ru", "-"}) {
                SaveRegLang(UString(code), settings);
                ReloadLang(settings);
                QtNotifyLanguageChanged();
                QCOMPARE(item->text(3),
                         QByteArray(code) == "ru" ? QStringLiteral("Ссылка") : QString("Link"));
                QCOMPARE(list->currentItem(), item);
                QCOMPARE(list->selectedItems(), QList<QTreeWidgetItem *>({item}));
            }
            observed = true;
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(observed);
        QVERIFY(QFileInfo(link).isSymLink());
    }
    void sourceInitialFrame_data() {
        QTest::addColumn<QString>("language");
        QTest::newRow("en") << QString("-");
        QTest::newRow("ru") << QString("ru");
    }
    void sourceInitialFrame() {
        QFETCH(QString, language);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        QVERIFY(file(owned.filePath("fixture.txt")));
        QVERIFY(inactive(owned.path()));
        bool inspected = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(UiInspection::writeReport(*dialog,
                                              QString("temp-source-initial-%1").arg(language)));
            QVERIFY(dialog->windowFlags().testFlag(Qt::WindowMinMaxButtonsHint));
            QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowContextHelpButtonHint));
            auto *parent = dialog->findChild<QPushButton *>("tempParent");
            QStyleOptionButton option;
            option.initFrom(parent);
            option.text = parent->text();
            const QRect contents =
                parent->style()->subElementRect(QStyle::SE_PushButtonContents, &option, parent);
            QVERIFY(parent->iconSize().width() <= contents.width());
            QVERIFY(parent->iconSize().height() <= contents.height());
            QVERIFY(!parent->accessibleName().isEmpty());
            QVERIFY(!parent->toolTip().isEmpty());
            QVERIFY(!dialog->findChild<QLineEdit *>("tempPath")->text().isEmpty());
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            QCOMPARE(list->columnCount(), 6);
            QCOMPARE(list->headerItem()->text(0), QtLangText(IDS_PROP_NAME));
            auto *scope = dialog->findChild<QLabel *>("tempScope");
            QVERIFY(UiInspection::fullyVisible(*scope, *dialog));
            QVERIFY(UiInspection::labelFits(*scope));
            QVERIFY(UiInspection::separate(*list, *scope, *dialog));
            for (auto *button : dialog->findChildren<QPushButton *>()) {
                QVERIFY(UiInspection::fullyVisible(*button, *dialog));
                QCOMPARE(button->isDefault(), button->objectName() == "tempClose");
            }
            QCOMPARE(dialog->focusWidget(), dialog->findChild<QPushButton *>("tempClose"));
            inspected = true;
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(inspected);
    }
    void sourceFocusedButtonsAndListEnter_data() {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("key");
        for (int scenario = 0; scenario != 3; ++scenario)
            for (int key : {int(Qt::Key_Return), int(Qt::Key_Enter)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(scenario).arg(key)))
                    << scenario << key;
    }
    void sourceFocusedButtonsAndListEnter() {
        QFETCH(int, scenario);
        QFETCH(int, key);
        QTemporaryDir owned, extra;
        QVERIFY(owned.isValid() && extra.isValid());
        QVERIFY(file(owned.filePath("fixture.txt")));
        QVERIFY(inactive(owned.path()));
        bool inspected = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto closeDialog = qScopeGuard([&] { dialog->reject(); });
            QVERIFY(QTest::qWaitForWindowExposed(dialog));
            dialog->activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(dialog));
            auto *close = dialog->findChild<QPushButton *>("tempClose");
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            auto *path = dialog->findChild<QLineEdit *>("tempPath");
            if (scenario == 0) {
                QCOMPARE(dialog->focusWidget(), close);
                QTest::keyClick(close, Qt::Key(key));
                QVERIFY(!dialog->isVisible());
            } else if (scenario == 1) {
                select(*dialog, owned.path());
                const QString root = path->text();
                path->setFocus();
                QCOMPARE(QApplication::focusWidget(), path);
                for (auto *button : dialog->findChildren<QPushButton *>())
                    QVERIFY2(!button->isDefault(), qPrintable(button->objectName()));
                QTest::keyClick(path, Qt::Key(key));
                QVERIFY(dialog->isVisible());
                QCOMPARE(path->text(), root); // Source OnOK ignores the read-only path.
                dialog->accept();
                QVERIFY(dialog->isVisible());
                QCOMPARE(path->text(), root);
                list->setFocus();
                QTest::keyClick(list, Qt::Key(key));
                QVERIFY(path->text() != root);
                auto *parent = dialog->findChild<QPushButton *>("tempParent");
                parent->setFocus();
                QTest::keyClick(parent, Qt::Key(key));
                QCOMPARE(path->text(), root);
                list->setFocus();
                dialog->accept();
                QVERIFY(dialog->isVisible());
                QVERIFY(path->text() != root);
                parent->setFocus();
                QTest::keyClick(parent, Qt::Key(key));
                QCOMPARE(path->text(), root);
            } else {
                QVERIFY(!row(*dialog, extra.path()));
                QVERIFY(inactive(extra.path()));
                auto *refresh = dialog->findChild<QPushButton *>("tempRefresh");
                refresh->setFocus();
                QTest::keyClick(refresh, Qt::Key(key));
                QVERIFY(row(*dialog, extra.path()));
            }
            inspected = true;
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(inspected);
    }
    void ownedFileCleanupAndLinkNavigationNeverReachUserTargets() {
        opened = QUrl();
        QDesktopServices::setUrlHandler("file", this, "captureFile");
        const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
        QTemporaryDir root;
        QTemporaryDir outside;
        QVERIFY(root.isValid() && outside.isValid());
        QString temporaryPath;
        {
            QTemporaryFile temporary(QDir::tempPath() + QStringLiteral("/cleanup файл-XXXXXX.txt"));
            QVERIFY(temporary.open());
            temporaryPath = temporary.fileName();
            temporary.setAutoRemove(false);
        }
        const auto cleanup = qScopeGuard([&] { QFile::remove(temporaryPath); });
        QVERIFY(file(outside.filePath("keep.txt")));
        const QString link = root.filePath("external link");
        QVERIFY(CreateNativeSymbolicLink(link, outside.path()));
        QVERIFY(QFileInfo(link).isSymLink());
        QVERIFY(inactive(root.path()) && inactive(temporaryPath));
        bool blocked = false, deleted = false, fileSelected = false, asked = false;
        bool linkSelected = false;
        QStringList visibleNames, messages;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            bool enteringLink = false;
            QTimer answers;
            connect(&answers, &QTimer::timeout, dialog, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    messages.append(box->text());
                    if (box->standardButtons().testFlag(QMessageBox::Yes)) {
                        asked = true;
                        box->button(QMessageBox::Yes)->click();
                    } else {
                        blocked |= enteringLink && box->icon() == QMessageBox::Warning;
                        box->button(QMessageBox::Ok)->click();
                    }
                }
            });
            answers.start(5);
            select(*dialog, root.path());
            QTest::keyClick(list, Qt::Key_Return);
            select(*dialog, link);
            linkSelected = row(*dialog, link) && row(*dialog, link)->isSelected();
            enteringLink = true;
            QTest::keyClick(list, Qt::Key_Return);
            enteringLink = false;
            QTest::keyClick(list, Qt::Key_Backspace);
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                visibleNames.append(list->topLevelItem(i)->text(0));
            select(*dialog, temporaryPath);
            fileSelected = row(*dialog, temporaryPath) &&
                           dialog->findChild<QPushButton *>("tempDelete")->isEnabled();
            dialog->findChild<QPushButton *>("tempDelete")->click();
            deleted = !QFileInfo::exists(temporaryPath);
            answers.stop();
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY2(linkSelected, qPrintable(messages.join('|')));
        QVERIFY2(blocked, qPrintable(messages.join('|')));
        QVERIFY(opened.isEmpty());
        QVERIFY(!visibleNames.contains("keep.txt"));
        QVERIFY2(fileSelected, qPrintable(visibleNames.join('|')));
        QVERIFY(asked);
        QVERIFY2(deleted, qPrintable(messages.join('|')));
        QVERIFY(QFileInfo::exists(outside.filePath("keep.txt")));
        QVERIFY(QFileInfo(link).isSymLink());
    }
    void sourceShowDotsAndSingleClickRetainExternalProtection() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(file(root.filePath("inside.txt")));
        QVERIFY(inactive(root.path()));
        CFmSettings prefs;
        prefs.Load(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        prefs.ShowDots = prefs.SingleClick = prefs.FullRow = prefs.ShowGrid = true;
        prefs.Save(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        opened = QUrl();
        QDesktopServices::setUrlHandler("file", this, "captureFile");
        bool dots = false, launchProtected = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            if (auto *item = row(*dialog, root.path()))
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  list->visualItemRect(item).center());
            dots = !list->findItems("..", Qt::MatchExactly).isEmpty();
            if (auto *item = row(*dialog, root.filePath("inside.txt")))
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  list->visualItemRect(item).center());
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                if (entry.path == root.path())
                    launchProtected = entry.externallyProtected && !entry.active;
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr, QDesktopServices::openUrl);
        QDesktopServices::unsetUrlHandler("file");
        QVERIFY(dots && launchProtected);
        QCOMPARE(QFileInfo(opened.toLocalFile()).canonicalFilePath(),
                 QFileInfo(root.filePath("inside.txt")).canonicalFilePath());
        QVERIFY(QFileInfo::exists(root.filePath("inside.txt")));
    }
    void sourceContextPropertiesShowNativeDetails() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(file(root.filePath("details.txt")));
        QVERIFY(inactive(root.path()));
        bool properties = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            select(*dialog, root.path());
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            QTimer choose;
            connect(&choose, &QTimer::timeout, dialog, [&] {
                if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    QAction *detail = nullptr;
                    for (auto *action : menu->actions())
                        if (!action->isSeparator()) {
                            if (action->data().toUInt() == 4)
                                detail = action;
                        }
                    if (detail) {
                        menu->setActiveAction(detail);
                        QTest::keyClick(menu, Qt::Key_Return);
                    } else
                        menu->close();
                } else if (auto *box =
                               qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    properties = box->text().contains("details.txt");
                    box->button(QMessageBox::Ok)->click();
                }
            });
            choose.start(5);
            const QPoint point = list->visualItemRect(list->currentItem()).center();
            QContextMenuEvent context(QContextMenuEvent::Mouse, point,
                                      list->viewport()->mapToGlobal(point));
            QApplication::sendEvent(list->viewport(), &context);
            choose.stop();
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(properties);
        QVERIFY(QFileInfo::exists(root.filePath("details.txt")));
    }
    void missingSelectedItemDoesNotConfirmAnotherEntry_data() {
        QTest::addColumn<bool>("remove");
        QTest::newRow("properties") << false;
        QTest::newRow("delete") << true;
    }
    void missingSelectedItemDoesNotConfirmAnotherEntry() {
        QFETCH(bool, remove);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString missing = root.filePath(QStringLiteral("исчезнувший 文件.txt"));
        const QString retained = root.filePath("keep.txt");
        QVERIFY(file(missing) && file(retained));
        QVERIFY(inactive(root.path()));
        bool refreshed = false;
        int errors = 0, confirmations = 0, properties = 0;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            select(*dialog, root.path());
            QTest::keyClick(list, Qt::Key_Return);
            select(*dialog, missing);
            QVERIFY(list->currentItem());
            QCOMPARE(list->currentItem()->text(0).normalized(QString::NormalizationForm_C),
                     QFileInfo(missing).fileName().normalized(QString::NormalizationForm_C));
            QVERIFY(QFile::remove(missing));
            QTimer answers;
            connect(&answers, &QTimer::timeout, dialog, [&] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                if (box->icon() == QMessageBox::Critical)
                    ++errors;
                else if (box->icon() == QMessageBox::Question)
                    ++confirmations;
                else
                    ++properties;
                box->done(box->standardButtons().testFlag(QMessageBox::No) ? QMessageBox::No
                                                                           : QMessageBox::Ok);
            });
            answers.start(5);
            QTest::keyClick(list, remove ? Qt::Key_Delete : Qt::Key_Return,
                            remove ? Qt::NoModifier : Qt::AltModifier);
            QCOMPARE(errors, 1);
            QCOMPARE(confirmations, 0);
            QCOMPARE(properties, 0);
            refreshed = !row(*dialog, missing) && row(*dialog, retained);
            select(*dialog, retained);
            QTest::keyClick(list, Qt::Key_Return, Qt::AltModifier);
            QCOMPARE(properties, 1);
            answers.stop();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(refreshed);
        QFile remaining(retained);
        QVERIFY(remaining.open(QIODevice::ReadOnly));
        QCOMPARE(remaining.readAll(), QByteArray("fixture"));
    }
    void confirmationControlsTheActualNativeDeleteLoop_data() {
        QTest::addColumn<int>("answer");
        QTest::newRow("yes") << int(QMessageBox::Yes);
        QTest::newRow("no") << int(QMessageBox::No);
        QTest::newRow("cancel") << int(QMessageBox::Cancel);
    }
    void confirmationControlsTheActualNativeDeleteLoop() {
        QFETCH(int, answer);
        QTemporaryDir owned(QDir::tempPath() + QStringLiteral("/cleanup имя с пробелом-XXXXXX"));
        QTemporaryDir unowned(QDir::tempPath() + "/7zO12345678-XXXXXX");
        QVERIFY(owned.isValid() && unowned.isValid());
        QVERIFY(file(owned.filePath(QStringLiteral("данные с пробелом.txt"))));
        QVERIFY(file(unowned.filePath("keep.txt")));
        QVERIFY(inactive(owned.path()));
        bool present = false, excluded = false, confirmed = false, enabled = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            present = row(*dialog, owned.path());
            excluded = !row(*dialog, unowned.path());
            select(*dialog, owned.path());
            auto *button = dialog->findChild<QPushButton *>("tempDelete");
            enabled = button->isEnabled();
            QTimer::singleShot(0, dialog, [&] {
                if (auto *question =
                        qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    confirmed = question->standardButtons().testFlag(QMessageBox::Cancel) &&
                                question->text().contains(QFileInfo(owned.path()).fileName());
                    question->button(QMessageBox::StandardButton(answer))->click();
                }
            });
            button->click();
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(present && excluded && confirmed && enabled);
        QCOMPARE(QFileInfo::exists(owned.path()), answer != QMessageBox::Yes);
        QVERIFY(QFileInfo::exists(unowned.filePath("keep.txt")));
    }
    void activeAndUnknownExternalSessionsSurviveActualDeleteAttempts() {
        QtOwnedTempDir active;
        QTemporaryDir protectedCopy;
        QVERIFY(active.isValid() && protectedCopy.isValid());
        QVERIFY(file(active.filePath("active.txt")));
        QVERIFY(file(protectedCopy.filePath("external.txt")));
        auto protectedLease = QtOwnedTempRegistry().RegisterCreated(protectedCopy.path());
        QVERIFY(protectedLease && protectedLease->ProtectExternal());
        protectedLease.reset();
        bool activeDisabled = false, unknownDisabled = false;
        int messages = 0;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            const auto close = qScopeGuard([&] { dialog->reject(); });
            select(*dialog, active.path());
            activeDisabled = row(*dialog, active.path()) &&
                             !dialog->findChild<QPushButton *>("tempDelete")->isEnabled();
            auto *scope = dialog->findChild<QLabel *>("tempScope");
            QVERIFY(scope->text().contains(row(*dialog, active.path())->toolTip(0)));
            QVERIFY(!row(*dialog, active.path())->toolTip(0).isEmpty());
            select(*dialog, protectedCopy.path());
            unknownDisabled = row(*dialog, protectedCopy.path()) &&
                              !dialog->findChild<QPushButton *>("tempDelete")->isEnabled();
            QVERIFY(scope->text().contains(row(*dialog, protectedCopy.path())->toolTip(0)));
            QVERIFY(!row(*dialog, protectedCopy.path())->toolTip(0).isEmpty());
            for (const char *language : {"-", "ru"}) {
                const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
                SaveRegLang(UString(language), settings);
                ReloadLang(settings);
                QtNotifyLanguageChanged();
                QVERIFY(scope->text().contains(row(*dialog, protectedCopy.path())->toolTip(0)));
                for (const QSize size : {QSize(900, 580), QSize(500, 320)}) {
                    dialog->resize(size);
                    QApplication::processEvents();
                    QVERIFY(UiInspection::fullyVisible(*scope, *dialog));
                    QVERIFY(UiInspection::labelFits(*scope));
                    QVERIFY(UiInspection::writeReport(
                        *dialog, QString("temp-protected-%1-%2").arg(language).arg(size.width())));
                }
            }
            QTimer answers;
            connect(&answers, &QTimer::timeout, dialog, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    ++messages;
                    box->button(box->standardButtons().testFlag(QMessageBox::Yes) ? QMessageBox::Yes
                                                                                  : QMessageBox::Ok)
                        ->click();
                }
            });
            answers.start(5);
            QTest::keyClick(dialog->findChild<QTreeWidget *>("tempList"), Qt::Key_Delete);
            answers.stop();
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(activeDisabled && unknownDisabled);
        QCOMPARE(messages, 2);
        QVERIFY(QFileInfo::exists(active.filePath("active.txt")));
        QVERIFY(QFileInfo::exists(protectedCopy.filePath("external.txt")));
    }
    void partialFailureStopsBeforeLaterSelectedItemsAndRefreshPreservesSelection() {
        QTemporaryDir first(QDir::tempPath() + "/cleanup-a-XXXXXX");
        QTemporaryDir second(QDir::tempPath() + "/cleanup-b-XXXXXX");
        QTemporaryDir third(QDir::tempPath() + "/cleanup-c-XXXXXX");
        QTemporaryDir backup;
        QVERIFY(first.isValid() && second.isValid() && third.isValid() && backup.isValid());
        QVERIFY(inactive(first.path()) && inactive(second.path()) && inactive(third.path()));
        const QString moved = backup.filePath("original second");
        bool selected = false, replaced = false, refreshed = false;
        int messages = 0;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            list->header()->sectionClicked(0);
            list->clearSelection();
            for (const auto &path : {first.path(), second.path(), third.path()})
                if (auto *item = row(*dialog, path))
                    item->setSelected(true);
            selected = list->selectedItems().size() == 3;
            QTimer answers;
            connect(&answers, &QTimer::timeout, dialog, [&] {
                if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                    ++messages;
                    if (box->standardButtons().testFlag(QMessageBox::Yes)) {
                        replaced =
                            QDir().rename(second.path(), moved) && QDir().mkpath(second.path());
                        box->button(QMessageBox::Yes)->click();
                    } else
                        box->button(QMessageBox::Ok)->click();
                }
            });
            answers.start(5);
            dialog->findChild<QPushButton *>("tempDelete")->click();
            answers.stop();
            dialog->findChild<QPushButton *>("tempRefresh")->click();
            auto *remaining = row(*dialog, third.path());
            refreshed = !row(*dialog, first.path()) && !row(*dialog, second.path()) && remaining &&
                        remaining->isSelected();
            dialog->reject();
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(selected && replaced && refreshed);
        QCOMPARE(messages, 2);
        QVERIFY(!QFileInfo::exists(first.path()));
        QVERIFY(QFileInfo::exists(second.path()));
        QVERIFY(QFileInfo::exists(moved));
        QVERIFY(QFileInfo::exists(third.path()));
    }
    void sourceLargeCountRestoresWaitCursorAfterPopulation() {
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        for (unsigned i = 0; i < 1001; ++i)
            QVERIFY(file(owned.filePath(QString::number(i))));
        QVERIFY(inactive(owned.path()));
        CBrowseDialog2 dialog;
        dialog.TempFolderPath =
            (QFileInfo(QDir::tempPath()).canonicalFilePath() + QDir::separator())
                .toStdWString()
                .c_str();
        auto *list = dialog.findChild<QTreeWidget *>("tempList");
        bool populatedWithWait = false;
        connect(list->model(), &QAbstractItemModel::rowsInserted, &dialog, [&] {
            if (auto *cursor = QApplication::overrideCursor())
                populatedWithWait |= cursor->shape() == Qt::WaitCursor;
        });
        const bool hadCursor = QApplication::overrideCursor() != nullptr;
        const auto oldShape = hadCursor ? QApplication::overrideCursor()->shape() : Qt::ArrowCursor;
        QTimer::singleShot(0, &dialog, &QDialog::reject);
        dialog.Create();
        QVERIFY(populatedWithWait);
        QCOMPARE(QApplication::overrideCursor() != nullptr, hadCursor);
        if (hadCursor)
            QCOMPARE(QApplication::overrideCursor()->shape(), oldShape);
        QVERIFY(QFileInfo::exists(owned.filePath("1000")));
    }
    void sourceRefreshAndSortKeysRetainSelectionAndBytes() {
        QTemporaryDir owned;
        QVERIFY(owned.isValid());
        QVERIFY(file(owned.filePath(QStringLiteral("я большой.txt"))));
        QVERIFY(file(owned.filePath("a small.txt")));
        {
            QFile large(owned.filePath(QStringLiteral("я большой.txt")));
            QVERIFY(large.open(QIODevice::Append));
            QCOMPARE(large.write("more bytes"), qint64(10));
        }
        QVERIFY(inactive(owned.path()));
        bool observed = false;
        QTimer::singleShot(0, qApp, [&] {
            auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            const auto close = qScopeGuard([&] { dialog->reject(); });
            auto *list = dialog->findChild<QTreeWidget *>("tempList");
            select(*dialog, owned.path());
            QTest::keyClick(list, Qt::Key_Return);
            select(*dialog, owned.filePath("a small.txt"));
            QVERIFY(file(owned.filePath(QStringLiteral("новый файл.txt"))));
            QTest::keyClick(list, Qt::Key_R, Qt::ControlModifier);
            QCOMPARE(list->topLevelItemCount(), 3);
            QCOMPARE(list->selectedItems().size(), 1);
            QCOMPARE(list->currentItem()->text(0), QString("a small.txt"));
            for (const auto key : {Qt::Key_F3, Qt::Key_F5, Qt::Key_F6}) {
                QTest::keyClick(list, key, Qt::ControlModifier);
                const int column = key == Qt::Key_F3 ? 0 : key == Qt::Key_F5 ? 1 : 2;
                QCOMPARE(list->header()->sortIndicatorSection(), column);
                QCOMPARE(list->header()->sortIndicatorOrder(),
                         column == 0 ? Qt::AscendingOrder : Qt::DescendingOrder);
                QCOMPARE(list->selectedItems().size(), 1);
                QCOMPARE(list->currentItem()->text(0), QString("a small.txt"));
                if (column == 2)
                    QCOMPARE(
                        list->topLevelItem(0)->text(0).normalized(QString::NormalizationForm_C),
                        QStringLiteral("я большой.txt"));
                QTest::keyClick(list, key, Qt::ControlModifier);
                QCOMPARE(list->header()->sortIndicatorOrder(),
                         column == 0 ? Qt::DescendingOrder : Qt::AscendingOrder);
            }
            QTest::keyClick(list, Qt::Key_A, Qt::ControlModifier);
            QCOMPARE(list->selectedItems().size(), 3);
            auto *scope = dialog->findChild<QLabel *>("tempScope");
            QVERIFY(scope->text().contains(QtSelectionStatusText(3, 3)));
            list->clearSelection();
            QVERIFY(scope->text().contains(QtSelectionStatusText(0, 3)));
            observed = true;
        });
        MyBrowseForTempFolder(nullptr);
        QVERIFY(observed);
        QFile large(owned.filePath(QStringLiteral("я большой.txt")));
        QVERIFY(large.open(QIODevice::ReadOnly));
        QCOMPARE(large.readAll(), QByteArray("fixturemore bytes"));
    }
    void sourceNavigationHelpAndNormalCompactGeometry() {
        QTemporaryDir owned;
        QTemporaryDir settings;
        QVERIFY(owned.isValid() && settings.isValid());
        QVERIFY(QDir().mkpath(owned.filePath(QStringLiteral("подпапка с пробелом"))));
        QVERIFY(file(owned.filePath(QStringLiteral("подпапка с пробелом/данные.txt"))));
        QVERIFY(inactive(owned.path()));
        bool navigated = true, geometry = true, reports = true, help = true;
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), settings.filePath("language.ini"));
            ReloadLang(settings.filePath("language.ini"));
            QTimer::singleShot(0, qApp, [&] {
                auto *dialog = dynamic_cast<CBrowseDialog2 *>(QApplication::activeModalWidget());
                if (!dialog) {
                    navigated = false;
                    return;
                }
                for (const QSize size : {QSize(900, 580), QSize(500, 320)}) {
                    dialog->resize(size);
                    QApplication::processEvents();
                    for (auto *button : dialog->findChildren<QPushButton *>())
                        geometry = geometry && UiInspection::fullyVisible(*button, *dialog);
                    for (const char *name : {"tempList", "tempPath", "tempScope"})
                        geometry = geometry && UiInspection::fullyVisible(
                                                   *dialog->findChild<QWidget *>(name), *dialog);
                    reports =
                        reports &&
                        UiInspection::writeReport(
                            *dialog, QString("temp-cleanup-%1-%2").arg(language).arg(size.width()));
                }
                auto *list = dialog->findChild<QTreeWidget *>("tempList");
                select(*dialog, owned.path());
                QTest::keyClick(list, Qt::Key_Return);
                navigated = navigated && list->topLevelItemCount() == 1 &&
                            list->topLevelItem(0)->text(0) == QStringLiteral("подпапка с пробелом");
                QTest::keyClick(list, Qt::Key_Return);
                navigated = navigated && list->topLevelItemCount() == 1 &&
                            list->topLevelItem(0)->text(0) == QStringLiteral("данные.txt");
                QTest::keyClick(list, Qt::Key_Backspace);
                QTest::keyClick(list, Qt::Key_Backspace);
                navigated = navigated && row(*dialog, owned.path()) &&
                            row(*dialog, owned.path())->isSelected();
                dialog->findChild<QPushButton *>("tempHelp")->click();
                auto *helpWindow = dialog->findChild<QDialog *>("helpWindow");
                help = help && helpWindow && helpWindow->isVisible();
                if (helpWindow) {
                    help = help &&
                           helpWindow->findChild<QTextBrowser *>("helpBrowser")->source().path() ==
                               "/upstream/7zip/Help/fm/temp.htm";
                    helpWindow->close();
                    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                    dialog->activateWindow();
                    list->setFocus();
                    QTest::qWait(10);
                    QTest::keyClick(list, Qt::Key_F1);
                    if (auto *again = dialog->findChild<QDialog *>("helpWindow")) {
                        help = help && again->isVisible();
                        again->close();
                    } else
                        help = false;
                }
                dialog->findChild<QPushButton *>("tempClose")->click();
            });
            MyBrowseForTempFolder(nullptr);
        }
        ReloadLang(settings.filePath("reset.ini"));
        QVERIFY(navigated && geometry && reports && help);
        QVERIFY(
            QFileInfo::exists(owned.filePath(QStringLiteral("подпапка с пробелом/данные.txt"))));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(TempCleanupTests)
#include "temp_cleanup_tests.moc"
