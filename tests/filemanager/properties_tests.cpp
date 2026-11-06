#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/property_projection_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/LangUtils.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QTreeWidget>
#include <QtTest>

class PropertiesTests : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static CMyComPtr<IFolderFolder> archiveFolder(const QList<ArchiveEntry> &entries) {
        CMyComPtr<IFolderFolder> folder;
        if (CreateArchiveFolder(entries, UString(L"7z"), &folder) != S_OK)
            return {};
        return folder;
    }
    static CRecordVector<UInt32> allIndices(IFolderFolder *folder) {
        CRecordVector<UInt32> indices;
        UInt32 count;
        if (folder->GetNumberOfItems(&count) == S_OK)
            for (UInt32 i = 0; i < count; ++i)
                indices.Add(i);
        return indices;
    }
    static QString value(const QDialog &dialog, const QString &name) {
        auto *list = dialog.findChild<QTreeWidget *>("propertiesList");
        if (!list) {
            QString caption = name;
            if (name == "Сжатый")
                caption = QCoreApplication::translate("PropertiesWindow",
                                                      "Reported packed size (may be partial)");
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->accessibleName() == caption)
                    return label->text();
            return QString();
        }
        const auto found = list->findItems(name, Qt::MatchExactly, 0);
        return found.isEmpty() ? QString() : found.first()->text(1);
    }

  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
    }
    void selectedFilesAndFolderShowSummary() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString original = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temporary.path()));
        for (const auto &name :
             {QStringLiteral("один файл.txt"), QStringLiteral("два файла.txt")}) {
            QFile file(temporary.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("hello"), qint64(5));
        }
        QVERIFY(QDir().mkdir(QStringLiteral("папка")));
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temporary.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        PropertyProjection dialog(panel, allIndices(folder));
        QCOMPARE(dialog.windowTitle(), QString("Свойства"));
        QCOMPARE(value(dialog, "Файлов"), QString("2"));
        QCOMPARE(value(dialog, "Папок"), QString("1"));
        QCOMPARE(value(dialog, "Размер"), QString("10"));
        QCOMPARE(value(dialog, "Сжатый"), QString("10"));
        QCOMPARE(QDir::fromNativeSeparators(value(dialog, "Имя")),
                 QFileInfo(temporary.path()).canonicalFilePath() + '/');
        QVERIFY(QDir::setCurrent(original));
    }

    void archiveSummaryIncludesPackedSize() {
        auto folder = archiveFolder({ArchiveEntry{"a.txt", 10, 4}, ArchiveEntry{"b.txt", 5, 2}});
        QVERIFY(folder);
        CPanel panel(folder);
        panel.SetCurrentFolderPrefix("archive.7z");
        PropertyProjection dialog(panel, allIndices(folder));
        QCOMPARE(value(dialog, "Сжатый"), QString("6"));
        QCOMPARE(value(dialog, "Размер"), QString("15"));
        QCOMPARE(value(dialog, "Файлов"), QString("2"));
    }

    void archiveFolderPropertiesIncludeNestedFiles() {
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, "folder.7z",
                          {ArchiveEntry{QStringLiteral("папка/внутри.txt"), 13, 5},
                           ArchiveEntry{QStringLiteral("папка/sub/ещё.txt"), 7, 3}});
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *action = window.findChild<QAction *>("selectedPropertiesAction");
        QCOMPARE(table->rowCount(), 1);
        table->selectRow(0);
        action->trigger();
        {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            QVERIFY(dialog);
            const auto closeDialog = qScopeGuard([dialog] { dialog->reject(); });
            QCOMPARE(value(*dialog, "Файлов"), QString("2"));
            QCOMPARE(value(*dialog, "Папок"), QString("1"));
            QCOMPARE(value(*dialog, "Размер"), QString("20"));
            QCOMPARE(value(*dialog, "Сжатый"), QString("8"));
            dialog->reject();
        }
    }

    void sourceSummaryUsesDescendantsOfTheSelectedFolder() {
        auto folder = archiveFolder({ArchiveEntry{QStringLiteral("папка/файл.txt"), 13, 5},
                                     ArchiveEntry{QStringLiteral("папка/sub/второй.txt"), 7, 3},
                                     ArchiveEntry{QStringLiteral("другой.txt"), 4, 2}});
        QVERIFY(folder);
        CPanel panel(folder);
        PropertyProjection dialog(panel, allIndices(folder));
        QCOMPARE(value(dialog, "Размер"), QString("24"));
        QCOMPARE(value(dialog, "Сжатый"), QString("10"));
        QCOMPARE(value(dialog, "Файлов"), QString("3"));
        QCOMPARE(value(dialog, "Папок"), QString("2"));
    }

    void singleFileFormatsSizeAndOmitsUnavailableDate() {
        auto folder = archiveFolder({ArchiveEntry{QStringLiteral("файл 😀.txt"), 12345, 3}});
        QVERIFY(folder);
        CPanel panel(folder);
        panel.SetCurrentFolderPrefix("archive.7z");
        PropertyProjection dialog(panel, allIndices(folder));
        QCOMPARE(value(dialog, "Имя"), QStringLiteral("файл 😀.txt"));
        QCOMPARE(value(dialog, "Размер"), QString("12 345"));
        QVERIFY(value(dialog, "Изменен").isNull());
        QCOMPARE(value(dialog, "Сжатый"), QString("3"));
        QCOMPARE(value(dialog, "Файлов"), QString("1")); // Source folder totals follow item.
        QCOMPARE(value(dialog, "Папок"), QString("0"));
    }

    void filesystemPropertiesUseLoadedMetadataUntilReload() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString original = QDir::currentPath();
        QVERIFY(QDir::setCurrent(temporary.path()));
        const QString name = QStringLiteral("файл й с пробелом.txt");
        QFile file(temporary.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture"), qint64(7));
        file.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(temporary.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        CPanel panel(folder);
        const auto indices = allIndices(folder);
        QCOMPARE(indices.Size(), unsigned(1));
        auto inspect = [&] {
            PropertyProjection dialog(panel, indices);
            QCOMPARE(value(dialog, "Имя").normalized(QString::NormalizationForm_C), name);
            QCOMPARE(value(dialog, "Размер"), QString("7"));
            QCOMPARE(value(dialog, "Сжатый"), QString("7"));
            QVERIFY(!value(dialog, "Изменен").isEmpty());
        };
        inspect(); // Also primes upstream's lazy packed-size cache on Windows.
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(QByteArray(23, 'x')), qint64(23));
        file.close();
        inspect(); // A row/QFileInfo reconstruction would incorrectly show 23.
        QVERIFY(QDir::setCurrent(original));
    }

    void archivePropertiesUsePresenceAndRetainSuppliedTimestampText() {
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, "metadata.7z",
                          {ArchiveEntry{"unknown.txt", -1, -1, "2026-09-28 12:34:56.1234567"},
                           ArchiveEntry{"zero.txt", 0, 0}});
        auto *table = window.findChild<FileTable *>("archiveTable");
        auto *action = window.findChild<QAction *>("selectedPropertiesAction");
        QVERIFY(table && action);
        int unknownRow = -1;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->model()->index(row, 0).data().toString() == "unknown.txt")
                unknownRow = row;
        QVERIFY(unknownRow >= 0);
        table->selectRow(unknownRow);
        // Presentation cells are not authoritative metadata.
        SetPanelData(table->model()->index(unknownRow, 2), "999", Qt::EditRole);
        SetPanelData(table->model()->index(unknownRow, 3), "888", Qt::EditRole);
        SetPanelData(table->model()->index(unknownRow, 4), "changed presentation", Qt::EditRole);
        action->trigger();
        {
            auto *dialog = window.findChild<QDialog *>("selectedPropertiesDialog");
            QVERIFY(dialog);
            const auto closeDialog = qScopeGuard([dialog] { dialog->reject(); });
            QCOMPARE(value(*dialog, "Имя"), QString("unknown.txt"));
            QCOMPARE(dialog->findChild<QLabel *>("propertiesSize")->text(), QString("Недоступно"));
            QCOMPARE(value(*dialog, "Изменен"), QString("2026-09-28 12:34:56.1234567"));
            dialog->reject();
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PropertiesTests)
#include "properties_tests.moc"
