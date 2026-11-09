#include "7zip/UI/FileManager/resource.h"
#include "archive/operations/ZipRegistry.h"
#include "dialogs/compress/createarchivedialog.h"
#include "dialogs/compress/qtcompressoptionsdialog.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/qtfmoptionsdialog.h"
#include "support/options_presentation.h"
#include "support/translation_catalog.h"
#include "ui/support/layout_inspection.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyleOptionButton>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>

class LanguageUiTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString settingsPath() const { return qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"); }
    void chooseLanguage(QDialog &dialog, int index) {
        auto *choice = dialog.findChild<QComboBox *>("languageChoice");
        QVERIFY(choice);
        const int language =
            choice->findText(index ? QStringLiteral("Русский") : "English", Qt::MatchContains);
        QVERIFY(language >= 0);
        choice->setCurrentIndex(language);
    }
    void apply(QDialog &dialog) {
        auto *buttons = dialog.findChild<QDialogButtonBox *>("settingsButtons");
        QVERIFY(buttons && buttons->button(QDialogButtonBox::Apply)->isEnabled());
        buttons->button(QDialogButtonBox::Apply)->click();
    }

  private slots:
    void legacyExternalToolsStayOutOfTranslatedFileMenu() {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        settings.setValue("FM/Diff", "fixture-diff-tool");
        settings.setValue("FM/7vc", "fixture-version-tool");
        settings.sync();
        const auto restore = qScopeGuard([&] {
            settings.remove("FM/Diff");
            settings.remove("FM/7vc");
            SaveRegLang(UString("-"), settingsPath());
            ReloadLang(settingsPath());
        });
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *file = window.findChild<QMenu *>("fileMenu");
        QVERIFY(file);
        for (const char *language : {"-", "ru"}) {
            SaveRegLang(UString(language), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            file->popup(window.mapToGlobal(QPoint(20, 40)));
            QApplication::processEvents();
            const auto close = qScopeGuard([file] { file->hide(); });
            for (const auto *action : file->actions()) {
                const unsigned command = action->property("nativeCommandID").toUInt();
                QVERIFY(command != IDM_DIFF && (command < IDM_VER_EDIT || command > IDM_VER_DIFF));
            }
        }
    }
    void mainMenusFollowLanguageAtStartupAndAfterReload() {
        ControlledBackend backend;
        for (const char *initial : {"-", "ru"}) {
            SaveRegLang(UString(initial), settingsPath());
            MainWindow window(&backend);
            window.show();
            auto *panels = window.findChild<QAction *>("twoPanelsAction");
            QVERIFY(panels);
            const bool checked = panels->isChecked();
            const QKeySequence shortcut = panels->shortcut();
            auto verify = [&](const QString &language) {
                auto *view = window.findChild<QMenu *>("viewMenu");
                view->popup(window.mapToGlobal(QPoint(20, 40)));
                QApplication::processEvents();
                view->hide();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QCOMPARE(panels->text(),
                         language == "ru" ? QStringLiteral("Две панели") : QString("Two panels"));
                QCOMPARE(panels->isChecked(), checked);
                QCOMPARE(panels->shortcut(), shortcut);
                const QStringList archiveCaptions =
                    language == "ru"
                        ? QStringList{QStringLiteral("Добавить"), QStringLiteral("Извлечь"),
                                      QStringLiteral("Тестировать")}
                        : QStringList{"Add", "Extract", "Test"};
                const char *archiveCommands[] = {"toolbarAddAction", "extractArchiveAction",
                                                 "testArchiveAction"};
                for (int i = 0; i < archiveCaptions.size(); ++i)
                    QCOMPARE(window.findChild<QAction *>(archiveCommands[i])->text(),
                             archiveCaptions[i]);
                if (language != "ru") {
                    const QRegularExpression cyrillic(QStringLiteral("[А-Яа-яЁё]"));
                    for (QMenu *menu : window.findChildren<QMenu *>())
                        for (QAction *action : menu->actions()) {
                            // Bookmark paths are user content, independent of UI language.
                            if (action->objectName().startsWith("openBookmark"))
                                continue;
                            QVERIFY2(!action->text().contains(cyrillic),
                                     qPrintable(action->objectName() + ": " + action->text()));
                        }
                }
            };
            verify(initial);
            for (const char *language : {"-", "ru", "-", "missing"}) {
                SaveRegLang(UString(language), settingsPath());
                ReloadLang(settingsPath());
                QtNotifyLanguageChanged();
                verify(language);
            }
        }
    }

    void toolbarTooltipsRetainSourceTextAfterActionChanges() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("toolbar файл 空.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("protected toolbar bytes"), qint64(23));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(directory.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(table->rowCount(), 1);
        auto *toolbar = window.findChild<QToolBar *>("panelFileActions0");
        QVERIFY(toolbar);
        const char *names[] = {"createFolderAction", "cutFilesAction", "copyFilesAction",
                               "pasteFilesAction",   "renameAction",   "deleteAction"};
        for (const QString &language : {QString("-"), QString("ru"), QString("-")}) {
            SaveRegLang(ToUString(language), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            for (bool selected : {false, true}) {
                table->clearSelection();
                table->setCurrentCell(-1, -1);
                if (selected) {
                    table->setCurrentCell(0, 0);
                    table->selectRow(0);
                }
                QApplication::processEvents();
                for (unsigned i = 0; i < 6; ++i) {
                    auto *source = window.findChild<QAction *>(names[i]);
                    QVERIFY(source);
                    QAction *proxy = nullptr;
                    for (auto *candidate : toolbar->actions())
                        if (candidate->property("sourceAction").value<QAction *>() == source)
                            proxy = candidate;
                    QVERIFY(proxy);
                    auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(proxy));
                    QVERIFY(button);
                    const QString expected = QString(source->text()).remove('&');
                    QCOMPARE(button->text(), expected);
                    if (i != 0) {
                        QCOMPARE(button->accessibleName(), expected);
                        QVERIFY(button->toolTip().contains(expected));
                        if (!source->shortcut().isEmpty())
                            QVERIFY(button->toolTip().contains(
                                source->shortcut().toString(QKeySequence::NativeText)));
                    }
                }
            }
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*toolbar, window));
                QVERIFY(UiInspection::writeReport(
                    window, QString("toolbar-text-%1-%2").arg(language).arg(size.width())));
            }
        }
        auto *copy = window.findChild<QAction *>("copyFilesAction");
        QToolButton *copyButton = nullptr;
        for (auto *proxy : toolbar->actions())
            if (proxy->property("sourceAction").value<QAction *>() == copy)
                copyButton = qobject_cast<QToolButton *>(toolbar->widgetForAction(proxy));
        QVERIFY(copyButton);
        const auto originalShortcut = copy->shortcut();
        const QKeySequence changedShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
        copy->setShortcut(changedShortcut);
        QVERIFY(copyButton->toolTip().contains(changedShortcut.toString(QKeySequence::NativeText)));
        copy->setShortcut(originalShortcut);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("protected toolbar bytes"));
    }

    void init() {
        QFile::remove(settingsPath());
        SaveRegLang(UString("-"), settingsPath());
        ReloadLang(settingsPath());
    }
    void cleanup() {
        SaveRegLang(UString("-"), settingsPath());
        ReloadLang(settingsPath());
    }
    void addDialogRetainsPendingConfiguration() {
        QTemporaryDir directory;
        CreateArchiveDialog dialog(directory.path());
        auto *name = dialog.findChild<QLineEdit *>("archiveName");
        auto *password = dialog.findChild<QLineEdit *>("archivePassword");
        auto *parameters = dialog.findChild<QLineEdit *>("archiveParameters");
        auto *level = dialog.findChild<QComboBox *>("compressionLevel");
        auto *dictionary = dialog.findChild<QComboBox *>("compressionDictionary");
        auto *volume = dialog.findChild<QComboBox *>("archiveVolumeSize");
        QVERIFY(name && password && parameters && level && dictionary && volume);
        name->setText(QStringLiteral("pending archive 密碼.7z"));
        password->setText(QStringLiteral("pending secret 密碼"));
        volume->setEditText("10M 20M");
        level->setCurrentIndex(level->count() - 1);
        dictionary->setCurrentIndex(dictionary->count() - 1);
        parameters->setText("mt=2 tm=off");
        const QVariant levelData = level->currentData();
        const QVariant dictionaryData = dictionary->currentData();
        const QString englishLevel = level->currentText();
        for (const char *lang : {"ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            QCOMPARE(name->text(), QStringLiteral("pending archive 密碼.7z"));
            QCOMPARE(password->text(), QStringLiteral("pending secret 密碼"));
            QCOMPARE(parameters->text(), QString("mt=2 tm=off"));
            QCOMPARE(volume->currentText(), QString("10M 20M"));
            QCOMPARE(level->currentData(), levelData);
            QCOMPARE(dictionary->currentData(), dictionaryData);
            if (QString(lang) == "ru") {
                QCOMPARE(dialog.windowTitle(), QStringLiteral("Добавить к архиву"));
                QVERIFY(level->currentText() != englishLevel);
            } else {
                QCOMPARE(dialog.windowTitle(), QString("Add to Archive"));
                QCOMPARE(level->currentText(), englishLevel);
            }
            for (QSize size : {QSize(760, 720), QSize(640, 480)}) {
                dialog.resize(size);
                dialog.show();
                QApplication::processEvents();
                auto *buttons = dialog.findChild<QDialogButtonBox *>();
                QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
                auto *scroll = dialog.findChild<QScrollArea *>("archiveOptionsScrollArea");
                QVERIFY(scroll && scroll->widget());
                QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
                for (QWidget *control : scroll->widget()->findChildren<QWidget *>()) {
                    if (control->objectName().isEmpty() ||
                        control->objectName().startsWith("qt_") || !control->isVisible())
                        continue;
                    if (!qobject_cast<QAbstractButton *>(control) &&
                        !qobject_cast<QComboBox *>(control) &&
                        !qobject_cast<QAbstractSpinBox *>(control) &&
                        !qobject_cast<QLineEdit *>(control) &&
                        !qobject_cast<QListWidget *>(control) && !qobject_cast<QLabel *>(control))
                        continue;
                    QApplication::processEvents();
                    // ensureWidgetVisible() uses the input-method cursor rect
                    // for editable controls. Geometry coverage needs the whole
                    // control, including a combo's frame and arrow.
                    const QPoint center =
                        control->mapTo(scroll->widget(), control->rect().center());
                    scroll->ensureVisible(center.x(), center.y(), control->width() / 2 + 5,
                                          control->height() / 2 + 30);
                    QApplication::processEvents();
                    if (!UiInspection::fullyVisible(*control, dialog))
                        QVERIFY(UiInspection::writeReport(dialog,
                                                          QString("language-add-clipped-%1-%2-%3")
                                                              .arg(lang)
                                                              .arg(size.width())
                                                              .arg(control->objectName())));
                    QVERIFY2(UiInspection::fullyVisible(*control, dialog),
                             qPrintable(control->objectName()));
                    if (qobject_cast<QLabel *>(control))
                        QVERIFY2(UiInspection::labelFits(*control),
                                 qPrintable(control->objectName()));
                    if (auto *button = qobject_cast<QAbstractButton *>(control)) {
                        if (button->objectName() == "archiveBrowse") {
                            // Source bxsDots is a narrow "..." button. The
                            // platform's footer-button sizeHint has an unrelated
                            // minimum width; check the actual caption bounds.
                            QStyleOptionButton option;
                            option.initFrom(button);
                            option.text = button->text();
                            const QRect contents = button->style()->subElementRect(
                                QStyle::SE_PushButtonContents, &option, button);
                            QVERIFY(button->fontMetrics().horizontalAdvance(button->text()) <=
                                    contents.width());
                        } else
                            QVERIFY2(control->sizeHint().width() <= control->width(),
                                     qPrintable(control->objectName()));
                    }
                    QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
                }
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("language-add-%1-%2").arg(lang).arg(size.width())));
                QVERIFY(!UiInspection::dump(dialog).contains("pending secret"));
            }
        }
    }
    void advancedDialogRetainsPendingTimePolicy() {
        NCompression::CFormatOptions profile;
        CCompressDialog source;
        source.Format = "zip";
        source.FormatOptions = &profile;
        source.ArcInfo.Name = L"zip";
        source.ArcInfo.Flags = NArcInfoFlags::kCTime | NArcInfoFlags::kATime |
                               NArcInfoFlags::kMTime | NArcInfoFlags::kMTime_Default;
        source.ArcInfo.TimeFlags = TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kWindows) |
                                   TIME_PREC_TO_ARC_FLAGS_MASK(NFileTimeType::kUnix);
        COptionsDialog dialog(&source);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));
        auto *precision = dialog.findChild<QComboBox *>("IDC_COMPRESS_TIME_PREC");
        auto *explicitPrecision = dialog.findChild<QCheckBox *>("IDX_COMPRESS_PREC_SET");
        auto *explicitArchiveTime = dialog.findChild<QCheckBox *>("IDX_COMPRESS_ZTIME_SET");
        explicitPrecision->click();
        precision->setCurrentIndex(precision->findData(UInt32(NFileTimeType::kUnix)));
        QMetaObject::invokeMethod(precision, "activated", Qt::DirectConnection,
                                  Q_ARG(int, precision->currentIndex()));
        explicitArchiveTime->click();
        const QString englishPrecision = precision->currentText();
        const QString englishOverride = explicitArchiveTime->text();
        const QString englishInfo = dialog.findChild<QLabel *>("IDT_COMPRESS_TIME_INFO")->text();
        for (const char *lang : {"ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            QCOMPARE(precision->currentData().toUInt(), UInt32(NFileTimeType::kUnix));
            QVERIFY(explicitPrecision->isChecked());
            QVERIFY(explicitArchiveTime->isChecked());
            QVERIFY(!profile.IsSet_TimePrec());
            if (QString(lang) == "ru") {
                QVERIFY(explicitArchiveTime->text() != englishOverride);
                QVERIFY(precision->currentText() != englishPrecision);
                QVERIFY(dialog.findChild<QLabel *>("IDT_COMPRESS_TIME_INFO")->text() !=
                        englishInfo);
            } else {
                QCOMPARE(explicitArchiveTime->text(), englishOverride);
                QCOMPARE(precision->currentText(), englishPrecision);
                QCOMPARE(dialog.findChild<QLabel *>("IDT_COMPRESS_TIME_INFO")->text(), englishInfo);
            }
            for (QSize size : {QSize(520, 400), QSize(440, 340)}) {
                dialog.resize(size);
                QApplication::processEvents();
                QTRY_COMPARE(dialog.size(), size.expandedTo(dialog.minimumSizeHint()));
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("language-advanced-%1-%2").arg(lang).arg(size.width())));
                for (auto *label : dialog.findChildren<QLabel *>())
                    if (label->isVisible()) {
                        QVERIFY(UiInspection::fullyVisible(*label, dialog));
                        QVERIFY(UiInspection::labelFits(*label));
                    }
                QVERIFY(UiInspection::writeReport(
                    dialog, QString("language-advanced-%1-%2").arg(lang).arg(size.width())));
            }
        }
    }
    void directoryTooltipsPreservePathsAcrossLanguageChange() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString child = directory.filePath(QStringLiteral("child 目录"));
        QVERIFY(QDir().mkpath(child));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(directory.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(table->rowCount(), 1);
        const QPersistentModelIndex item = table->model()->index(0, 0);
        const QString tooltip = item.data(Qt::ToolTipRole).toString();
        QCOMPARE(tooltip, QFileInfo(child).canonicalFilePath());
        table->selectRow(0);
        for (const char *lang : {"ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            QCOMPARE(table->rowCount(), 1);
            QCOMPARE(table->model()->index(0, 0), item);
            QCOMPARE(item.data(Qt::ToolTipRole).toString(), tooltip);
            QVERIFY(PanelIndexSelected(item));
            QCOMPARE(address->text(), QFileInfo(directory.path()).canonicalFilePath());
            QCOMPARE(address->placeholderText(), QString(lang) == "ru"
                                                     ? QStringLiteral("Путь к папке или архиву")
                                                     : QString("Path to folder or archive"));
        }
    }
    void propertiesPreserveRowsValuesAndSelection() {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("property 密碼 file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(directory.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(table->rowCount(), 1);
#ifdef Q_OS_WIN
        // Windows filesystem Properties belongs to the OS. Qt language changes
        // apply to the source archive collector, whose rows this test inspects.
        openListedArchive(window, backend, "language-properties.zip",
                          {{QFileInfo(file.fileName()).fileName(), 7, 7}});
#endif
        table->selectRow(0);
        window.findChild<QAction *>("selectedPropertiesAction")->trigger();
        auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        const auto close = qScopeGuard([dialog] { dialog->close(); });
        auto *location = dialog->findChild<QLabel *>("propertiesLocation");
        auto *sizeValue = dialog->findChild<QLabel *>("propertiesSize");
        QVERIFY(location && sizeValue);
        const QString originalLocation = location->text();
        const QString originalSize = sizeValue->text();
        QVERIFY(!originalLocation.isEmpty());
        QVERIFY(!originalSize.isEmpty());
        for (const char *lang : {"ru", "-"}) {
            SaveRegLang(UString(lang), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            QCOMPARE(dialog->windowTitle(),
                     QString(lang) == "ru" ? QStringLiteral("Свойства") : QString("Properties"));
            QCOMPARE(location->text(), originalLocation);
            QCOMPARE(sizeValue->text(), originalSize);
            QVERIFY(table->selectionModel()->isRowSelected(0, {}));
            for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
                dialog->resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*location, *dialog));
                QVERIFY(UiInspection::writeReport(
                    *dialog, QString("language-properties-%1-%2").arg(lang).arg(size.width())));
            }
        }
    }
    void applyCancelRestartAndMainWindowState() {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("имя с пробелом.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(directory.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(table->rowCount(), 1);
        table->selectRow(0);
        table->setCurrentCell(0, 0);
        auto *rename = window.findChild<QAction *>("renameAction");
        const QKeySequence shortcut = rename->shortcut();
        const bool enabled = rename->isEnabled();
        const QStringList fileNames = {table->model()->index(0, 0).data().toString()};
        auto *fileMenu = window.findChild<QMenu *>("fileMenu");
        auto *parentButton = window.findChild<QAbstractButton *>("panelParentButton0");
        QVERIFY(parentButton && !parentButton->icon().isNull());
        QVERIFY(parentButton->text().isEmpty());
        QCOMPARE(fileMenu->title(), QString("&File"));
        bool applied = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<QDialog *>("settingsDialog");
            QVERIFY(dialog);
            chooseLanguage(*dialog, 1);
            apply(*dialog);
            QCOMPARE(dialog->windowTitle(), QStringLiteral("Настройки"));
            QCOMPARE(fileMenu->title(), QStringLiteral("&Файл"));
            QVERIFY(!parentButton->icon().isNull());
            auto *choice = dialog->findChild<QComboBox *>("languageChoice");
            QVERIFY(choice->currentText().contains(QStringLiteral("Русский")));
            chooseLanguage(*dialog, 0);
            dialog->reject();
            applied = true;
        });
        window.findChild<QAction *>("settingsAction")->trigger();
        QVERIFY(applied);
        UString saved;
        ReadRegLang(saved, settingsPath());
        QCOMPARE(QString::fromWCharArray(saved), QString("ru"));
        QCOMPARE(window.findChild<QAction *>("renameAction"), rename);
        QCOMPARE(rename->shortcut(), shortcut);
        QCOMPARE(rename->isEnabled(), enabled);
        QTRY_COMPARE(table->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 0).data().toString(), fileNames.first());
        QCOMPARE(table->currentRow(), 0);
        QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(table->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Имя"));
        QCOMPARE(address->placeholderText(), QStringLiteral("Путь к папке или архиву"));
        QCOMPARE(address->text(), QFileInfo(directory.path()).canonicalFilePath());
        QCOMPARE(table->accessibleName(), QStringLiteral("Список файлов"));
        QCOMPARE(window.findChild<QListView *>("iconList")->accessibleName(),
                 QStringLiteral("Список файлов"));
        auto *toolbar = window.findChild<QToolBar *>("panelFileActions0");
        auto inspectWindow = [&](const QString &language) {
            for (QSize size : {QSize(1100, 640), QSize(640, 380)}) {
                window.resize(size);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*window.findChild<QWidget *>("panelNavigation0"),
                                                   window));
                QVERIFY(UiInspection::fullyVisible(*table, window));
                QVERIFY(UiInspection::fullyVisible(*toolbar, window));
                QVERIFY(UiInspection::writeReport(
                    window, QString("language-main-%1-%2").arg(language).arg(size.width())));
            }
        };
        inspectWindow("ru");
        QCOMPARE(window.findChild<QAction *>("backAction")->text(), QStringLiteral("Назад"));
        QCOMPARE(window.findChild<QAction *>("openItemAction")->text(), QStringLiteral("&Открыть"));
        QCOMPARE(window.findChild<QAction *>("editFileAction")->text(),
                 QStringLiteral("&Редактировать"));
        auto *copy = window.findChild<QAction *>("copyFilesAction");
        QToolButton *button = nullptr;
        for (auto *proxy : toolbar->actions())
            if (proxy->property("sourceAction").value<QAction *>() == copy)
                button = qobject_cast<QToolButton *>(toolbar->widgetForAction(proxy));
        QVERIFY(button);
        QCOMPARE(button->text(), QStringLiteral("Копировать"));
        QtFmOptionsDialog restarted(settingsPath());
        QVERIFY(restarted.findChild<QComboBox *>("languageChoice")
                    ->currentText()
                    .contains(QStringLiteral("Русский")));
        QCOMPARE(restarted.windowTitle(), QStringLiteral("Настройки"));
        MainWindow restartedWindow(&backend);
        QCOMPARE(restartedWindow.findChild<QMenu *>("fileMenu")->title(), QStringLiteral("&Файл"));
        chooseLanguage(restarted, 0);
        apply(restarted);
        QCOMPARE(fileMenu->title(), QString("&File"));
        QCOMPARE(window.findChild<QAction *>("backAction")->text(), QString("Back"));
        QCOMPARE(window.findChild<QAction *>("openItemAction")->text(), QString("&Open"));
        QCOMPARE(window.findChild<QAction *>("editFileAction")->text(), QString("&Edit"));
        QVERIFY(!parentButton->icon().isNull());
        QCOMPARE(button->text(), QString("Copy"));
        QCOMPARE(table->model()->headerData(0, Qt::Horizontal).toString(), QString("Name"));
        QCOMPARE(address->placeholderText(), QString("Path to folder or archive"));
        QCOMPARE(table->accessibleName(), QString("File list"));
        QCOMPARE(window.findChild<QListView *>("iconList")->accessibleName(), QString("File list"));
        inspectWindow("en");
    }
    void retranslationPreservesPendingInputAndGeometry() {
        QtFmOptionsDialog dialog(settingsPath());
        dialog.show();
        auto *pages = dialog.findChild<QTabWidget *>("optionsPages");
        auto *workPath = dialog.findChild<QLineEdit *>("workPath");
        dialog.findChild<QRadioButton *>("workSpecified")->click();
        auto *items = dialog.findChild<QListWidget *>("menuItems");
        auto *parentRow = dialog.findChild<QCheckBox *>("showDotsOption");
        const QString path = QStringLiteral("/tmp/папка с пробелом");
        workPath->setText(path);
        parentRow->setChecked(!parentRow->isChecked());
        const bool checked = parentRow->isChecked();
        const Qt::CheckState itemState = items->item(0)->checkState();
        for (const char *language : {"ru", "-"}) {
            SaveRegLang(UString(language), settingsPath());
            ReloadLang(settingsPath());
            QtNotifyLanguageChanged();
            QCOMPARE(dialog.windowTitle(),
                     QString(language) == "ru" ? QStringLiteral("Настройки") : QString("Options"));
            QCOMPARE(workPath->text(), path);
            QCOMPARE(parentRow->isChecked(), checked);
            QCOMPARE(items->item(0)->checkState(), itemState);
            QVERIFY(items->isEnabled());
            QVERIFY(items->toolTip().isEmpty());
            QVERIFY(dialog.findChild<QDialogButtonBox *>("settingsButtons")
                        ->button(QDialogButtonBox::Apply)
                        ->isEnabled());
            for (QSize size : {QSize(640, 390), QSize(520, 380), QSize(640, 720)}) {
                dialog.resize(size);
                for (const char *name :
                     {"appearanceChoice", "languageChoice", "workPathBrowse", "workPath",
                      "extractMemoryLabel", "extractMemorySpin", "showDotsOption"}) {
                    auto *control = dialog.findChild<QWidget *>(name);
                    QVERIFY(OptionsPresentation::show(dialog, control));
                    QVERIFY2(UiInspection::fullyVisible(*control, dialog), name);
                    if (qobject_cast<QLabel *>(control))
                        QVERIFY2(UiInspection::labelFits(*control), name);
                }
                for (int page = 0; page < pages->count(); ++page) {
                    pages->setCurrentIndex(page);
                    QApplication::processEvents();
                    QVERIFY(UiInspection::fullyVisible(
                        *dialog.findChild<QDialogButtonBox *>("settingsButtons"), dialog));
                    QVERIFY(
                        UiInspection::writeReport(dialog, QString("options-language-%1-%2x%3-%4")
                                                              .arg(language)
                                                              .arg(size.width())
                                                              .arg(size.height())
                                                              .arg(page)));
                }
            }
        }
        dialog.reject();
        NWorkDir::CInfo saved;
        saved.Load(settingsPath());
        QCOMPARE(saved.Mode, NWorkDir::NMode::kSystem);
    }
    void invalidSettingsKeepLanguagePending() {
        QtFmOptionsDialog dialog(settingsPath());
        dialog.show();
        chooseLanguage(dialog, 1);
        dialog.findChild<QCheckBox *>("extractMemorySet")->setChecked(true);
        auto *memory = dialog.findChild<QLineEdit *>("extractMemoryEdit");
        memory->setText("3GB");
        bool errorShown = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *error = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY(error);
            errorShown = true;
            error->accept();
        });
        apply(dialog);
        QVERIFY(errorShown);
        QCOMPARE(dialog.windowTitle(), QString("Options"));
        QVERIFY(dialog.findChild<QComboBox *>("languageChoice")
                    ->currentText()
                    .contains(QStringLiteral("Русский")));
        QCOMPARE(memory->text(), QString("3GB"));
        UString saved;
        ReadRegLang(saved, settingsPath());
        QCOMPARE(QString::fromWCharArray(saved), QString("-"));
        memory->setText("2");
        apply(dialog);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Настройки"));
        ReadRegLang(saved, settingsPath());
        QCOMPARE(QString::fromWCharArray(saved), QString("ru"));
    }
    void partialLanguageResetsMenusAndKeepsSpecialItems() {
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *openInside = window.findChild<QAction *>("openInsideAction");
        openInside->setText(openInside->text() + "\tCtrl+PgDown");
        auto *hash = window.findChild<QMenu *>("hashMenu");
        const QString hashTitle = hash->title();
        auto *favorite = window.findChild<QMenu *>("addFavoriteMenu");
        const QString favoriteSlot = favorite->actions().first()->text();
        const QString originalRename = window.findChild<QAction *>("renameAction")->text();
        QTemporaryDir directory;
        QVERIFY(WriteTranslationCatalog(directory.path(), {{541, "&Inside"}}));
        SaveRegLang(UString("de"), settingsPath());
        ReloadLang(settingsPath(), directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(openInside->text(), QString("&Inside"));
        QCOMPARE(window.findChild<QAction *>("renameAction")->text(), originalRename);
        QCOMPARE(hash->title(), hashTitle);
        QCOMPARE(favorite->actions().first()->text(), favoriteSlot);
        SaveRegLang(UString("ru"), settingsPath());
        ReloadLang(settingsPath());
        QtNotifyLanguageChanged();
        QVERIFY(window.findChild<QAction *>("renameAction")->text() != originalRename);
        SaveRegLang(UString("missing"), settingsPath());
        ReloadLang(settingsPath(), directory.path());
        QtNotifyLanguageChanged();
        QCOMPARE(window.findChild<QAction *>("renameAction")->text(), originalRename);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(LanguageUiTests)
#include "language_ui_tests.moc"
