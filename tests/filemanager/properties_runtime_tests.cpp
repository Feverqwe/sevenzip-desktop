#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/support/layout_inspection.h"
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QScrollArea>
#include <QTreeWidget>

class PropertiesRuntimeTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT

    struct Inspection {
        QMap<QString, QString> values;
        QString folderPath;
        bool shown = false;
        bool geometry = true;
    };

    static void createFile(const QString &path, int size) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(QByteArray(size, 'x')), qint64(size));
    }
    static void navigate(MainWindow &window, const QString &path) {
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
    }
    static int diskRow(MainWindow &window, const QString &path) {
        auto *view = table(window);
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 1).data().toString() == path)
                return row;
        return -1;
    }
    static void selectOnly(MainWindow &window, int row) {
        QVERIFY(row >= 0);
        auto *view = table(window);
        view->clearSelection();
        view->selectionModel()->select(view->model()->index(row, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    static Inspection inspect(MainWindow &window, const QString &reportTag = {},
                              const char *command = "selectedPropertiesAction") {
        Inspection result;
        action(window, command)->trigger();
        {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            if (!dialog)
                return result;
            result.shown =
                dialog->windowTitle() == QStringLiteral("Свойства") && !dialog->isModal();
            if (auto *selection = dialog->findChild<QWidget *>("propertiesSelection")) {
                for (auto *label : selection->findChildren<QLabel *>()) {
                    QString caption = label->accessibleName();
                    if (caption == QCoreApplication::translate(
                                       "PropertiesWindow", "Reported packed size (may be partial)"))
                        caption = "Сжатый";
                    if (caption == QCoreApplication::translate("PropertiesWindow",
                                                               "Reported size (may be partial)"))
                        caption = "Размер";
                    if (!caption.isEmpty())
                        result.values.insert(caption, label->text());
                }
            }
            if (auto *folder = dialog->findChild<QWidget *>("propertiesFolder")) {
                for (auto *label : folder->findChildren<QLabel *>())
                    if (label->accessibleName() ==
                        QCoreApplication::translate("PropertiesWindow", "Location")) {
                        result.folderPath = label->text();
                        break;
                    }
            }
            if (!reportTag.isEmpty()) {
                auto *buttons = dialog->findChild<QDialogButtonBox *>("propertiesActions");
                auto *body = dialog->findChild<QScrollArea *>();
                result.geometry = buttons && body;
                for (const QSize &size : {QSize(540, 360), QSize(360, 240)}) {
                    dialog->resize(size);
                    QCoreApplication::processEvents();
                    result.geometry &= body && UiInspection::fullyVisible(*body, *dialog);
                    if (buttons) {
                        result.geometry &= UiInspection::fullyVisible(*buttons, *dialog) &&
                                           UiInspection::separate(*body, *buttons, *dialog);
                        for (auto *button : buttons->buttons())
                            result.geometry &= UiInspection::fullyVisible(*button, *dialog);
                    }
                    result.geometry &= UiInspection::writeReport(
                        *dialog, reportTag + QString("-%1").arg(size.width()));
                }
            }
            dialog->reject();
            QCoreApplication::sendPostedEvents(dialog, QEvent::DeferredDelete);
        }
        return result;
    }

  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
    }
    void sourceOwnerAcrossViewsAndLocations_data() {
        QTest::addColumn<bool>("flat");
        QTest::addColumn<int>("mode");
        for (bool flat : {false, true})
            for (int mode = 0; mode < 4; ++mode)
                QTest::newRow(qPrintable(QString("flat%1-mode%2").arg(flat).arg(mode)))
                    << flat << mode;
    }
    void sourceOwnerAcrossViewsAndLocations() {
        QFETCH(bool, flat);
        QFETCH(int, mode);
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QDir root(QFileInfo(temporary.path()).canonicalFilePath());
        const QString directory = QStringLiteral("папка й с пробелом");
        const QString name = QStringLiteral("файл й.txt");
        QVERIFY(root.mkdir(directory));
        createFile(root.filePath(name), 7);
        createFile(root.filePath(directory + '/' + name), 11);
        createFile(root.filePath("zero"), 0);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "flatViewAction")->setChecked(false);
        action(window, qPrintable(QString("viewMode%1Action").arg(mode)))->trigger();
        navigate(window, root.path());
        action(window, "flatViewAction")->setChecked(flat);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        Inspection properties;
#ifndef Q_OS_WIN
        selectOnly(window, diskRow(window, root.filePath(name)));
        properties = inspect(window, {}, "archiveInfoAction");
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Имя").normalized(QString::NormalizationForm_C), name);
        QCOMPARE(properties.values.value("Размер"), QString("7"));
        QCOMPARE(properties.values.value("Сжатый"), QString("7"));
        QCOMPARE(QDir::fromNativeSeparators(properties.folderPath), root.path() + '/');
        QVERIFY(!properties.values.value("Изменен").isEmpty());
        // Prime the source's lazy Windows packed-size cache before changing the file.
        createFile(root.filePath(name), 31);
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Размер"), QString("7"));
        QCOMPARE(properties.values.value("Сжатый"), QString("7"));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        selectOnly(window, diskRow(window, root.filePath(name)));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Размер"), QString("31"));
        QCOMPARE(properties.values.value("Сжатый"), QString("31"));
        if (flat) {
            selectOnly(window, diskRow(window, root.filePath(directory + '/' + name)));
            properties = inspect(window);
            QVERIFY(properties.shown);
            QCOMPARE(properties.values.value("Имя").normalized(QString::NormalizationForm_C), name);
            QCOMPARE(properties.values.value("Размер"), QString("11"));
            QCOMPARE(QDir::fromNativeSeparators(properties.values.value("Путь"))
                         .normalized(QString::NormalizationForm_C),
                     directory + '/');
        }
        selectOnly(window, diskRow(window, root.filePath(directory)));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QVERIFY(!properties.values.contains("Размер"));
        QVERIFY(!properties.values.contains("Файлов"));
        QVERIFY(!properties.values.contains("Папок"));
#endif
        openListedArchive(window, backend, "properties-runtime.zip",
                          {{name, 17, 5, "2026-09-29 12:34:56.1234567"},
                           {directory + '/' + name, 23, 9},
                           {"zero", 0, 0},
                           {"unknown", -1, -1}});
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        action(window, "flatViewAction")->setChecked(flat);
        selectOnly(window, rowWithPath(window, name));
        // Sorting/display text cannot change source metadata or operated indices.
        const int row = rowWithPath(window, name);
        SetPanelData(table(window)->model()->index(row, 2), "999", Qt::EditRole);
        SetPanelData(table(window)->model()->index(row, 3), "888", Qt::EditRole);
        properties =
            inspect(window, QString("properties-runtime-flat%1-mode%2").arg(flat).arg(mode));
        QVERIFY(properties.shown && properties.geometry);
        QCOMPARE(properties.values.value("Имя"), name);
        QCOMPARE(properties.values.value("Размер"), QString("17"));
        QCOMPARE(properties.values.value("Сжатый"), QString("5"));
        QCOMPARE(properties.values.value("Изменен"), QString("2026-09-29 12:34:56.1234567"));
        QVERIFY(!properties.values.contains("Файлов"));
        QVERIFY(!properties.values.contains("Папок"));
        selectOnly(window, rowWithPath(window, "unknown"));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Имя"), QString("unknown"));
        QVERIFY(!properties.values.contains("Размер"));
        QVERIFY(!properties.values.contains("Сжатый"));
        QVERIFY(!properties.values.contains("Изменен"));
        selectOnly(window, rowWithPath(window, "zero"));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Размер"), QString("0"));
        QCOMPARE(properties.values.value("Сжатый"), QString("0"));
        selectOnly(window, rowWithPath(window, directory));
        properties =
            inspect(window, QString("properties-directory-flat%1-mode%2").arg(flat).arg(mode));
        QVERIFY(properties.shown && properties.geometry);
        QCOMPARE(properties.values.value("Имя"), directory);
        QCOMPARE(properties.values.value("Файлов"), QString("1"));
        QCOMPARE(properties.values.value("Папок"), QString("0"));
        QCOMPARE(properties.values.contains("Размер"), !flat);
        QCOMPARE(properties.values.contains("Сжатый"), !flat);
        if (!flat) {
            QCOMPARE(properties.values.value("Размер"), QString("23"));
            QCOMPARE(properties.values.value("Сжатый"), QString("9"));
        } else {
            selectOnly(window, rowWithPath(window, directory + '/' + name));
            properties = inspect(window);
            QVERIFY(properties.shown);
            QCOMPARE(properties.values.value("Имя"), name);
            QCOMPARE(properties.values.value("Размер"), QString("23"));
            QCOMPARE(properties.values.value("Сжатый"), QString("9"));
            QCOMPARE(QDir::fromNativeSeparators(properties.values.value("Путь")), directory + '/');
        }
        selectOnly(window, rowWithPath(window, directory));
        table(window)->selectionModel()->select(
            table(window)->model()->index(rowWithPath(window, name), 0),
            QItemSelectionModel::Select | QItemSelectionModel::Rows);
        properties =
            inspect(window, QString("properties-summary-flat%1-mode%2").arg(flat).arg(mode));
        QVERIFY(properties.shown && properties.geometry);
        QCOMPARE(properties.values.value("Размер"), flat ? QString("17") : QString("40"));
        QCOMPARE(properties.values.value("Сжатый"), flat ? QString("5") : QString("14"));
        QCOMPARE(properties.values.value("Папок"), QString("1"));
        QCOMPARE(properties.values.value("Файлов"), QString("2"));
        QCOMPARE(backend.extractCalls, 0);
#ifndef Q_OS_WIN
        navigate(window, root.path());
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        selectOnly(window, diskRow(window, root.filePath(name)));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Размер"), QString("31"));
        QCOMPARE(properties.values.value("Сжатый"), QString("31"));
        QCOMPARE(QDir::fromNativeSeparators(properties.folderPath), root.path() + '/');
        selectOnly(window, diskRow(window, root.filePath("zero")));
        properties = inspect(window);
        QVERIFY(properties.shown);
        QCOMPARE(properties.values.value("Размер"), QString("0"));
        QCOMPARE(properties.values.value("Сжатый"), QString("0"));
        QCOMPARE(backend.extractCalls, 0);
#endif
        action(window, "flatViewAction")->setChecked(false);
    }
};

QTEST_MAIN(PropertiesRuntimeTests)
#include "properties_runtime_tests.moc"
