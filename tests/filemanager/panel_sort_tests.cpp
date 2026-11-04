#include "filemanager/sorting/filenamesort.h"
#include "filemanager/support/window_state_support.h"
#include "ui/support/layout_inspection.h"
#include <QHeaderView>

class PanelSortTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private:
    static QStringList names(MainWindow &window) {
        QStringList result;
        for (int row = 0; row < table(window)->rowCount(); ++row)
            result << table(window)->model()->index(row, 0).data().toString();
        return result;
    }

  private slots:
    void init() {
        QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
        settings.remove("view/flatDisk");
        settings.remove("view/flatArchive");
        settings.remove("Columns");
    }

    void upstreamNaturalNameComparison() {
        QCOMPARE(CompareFileNames_ForFolderList(u"file0002", u"file2"), 0);
        QVERIFY(CompareFileNames_ForFolderList(u"file9", u"file10") < 0);
        QVERIFY(CompareFileNames_ForFolderList(u"99999999999999999999", u"100000000000000000000") <
                0);
        QCOMPARE(CompareFileNames_ForFolderList(u"Альфа файл01.TXT", u"альфа файл1.txt"), 0);
        QVERIFY(CompareFileNames_ForFolderList(u"file0", u"file00a") < 0);
        QCOMPARE(CompareFileNames_ForFolderList(u"", u""), 0);
        QVERIFY(CompareFileNames_ForFolderList(u"", u"0") < 0);
        const wchar_t *missing = nullptr;
        QCOMPARE(CompareFileNames_ForFolderList(missing, L""), 0);
        QVERIFY(CompareFileNames_ForFolderList(L"a", missing) > 0);
        const QString first = QStringLiteral("--file2 trailing");
        const QString second = QStringLiteral("--file10 trailing");
        QVERIFY(CompareFileNames_ForFolderList(QStringView(first).mid(2, 5),
                                               QStringView(second).mid(2, 6)) < 0);
    }

    void nativeCasePolicyPrecedesNaturalDigitComparison() {
        // Windows CharUpperW and source POSIX towupper pair these Greek
        // small/capital forms; Qt's single-QChar uppercase leaves U+1F80.
        QTemporaryDir temp;
        ControlledBackend backend;
        MainWindow window(&backend);
        const QString lower = QStringLiteral("\u1F80file10.txt");
        const QString upper = QStringLiteral("\u1F88file2.txt");
        openListedArchive(window, backend, temp.filePath("native case.7z"),
                          {{lower, 1, 1}, {upper, 1, 1}});
        QCOMPARE(names(window), (QStringList{upper, lower}));
        QCOMPARE(CompareFileNames_ForFolderList(u"\u1F80file2", u"\u1F88file2"), 0);
        table(window)->selectRow(rowWithPath(window, upper));
        action(window, "sort3Action")->trigger();
        QCOMPARE(names(window), (QStringList{lower, upper}));
        const auto selected = table(window)->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().data().toString(), upper);
    }
    void filesystemUsesTheSameNativeCaseAndNumericOrder() {
        QTemporaryDir temp;
        const QString lower = QStringLiteral("\u1F80file10.txt");
        const QString upper = QStringLiteral("\u1F88file2.txt");
        for (const auto &name : {lower, upper}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("owned"), qint64(5));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto normalizedNames = [&] {
            QStringList result = names(window);
            for (auto &name : result)
                name = name.normalized(QString::NormalizationForm_C);
            return result;
        };
        QTRY_COMPARE(normalizedNames(), (QStringList{upper, lower}));
        action(window, "sort3Action")->trigger();
        QCOMPARE(normalizedNames(), (QStringList{lower, upper}));
        QFile file(temp.filePath(upper));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("owned"));
    }
    void flatArchiveSortsNamesBeforePrefixes() {
        QTemporaryDir temp;
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, temp.filePath("flat.7z"),
                          {{"a/file10.txt", 5, 1},
                           {"z/file2.txt", 5, 1},
                           {"dir10/common.txt", 5, 1},
                           {"dir2/common.txt", 5, 1},
                           {QStringLiteral("папка с пробелом/Альфа.txt"), 5, 1}});
        action(window, "flatViewAction")->setChecked(true);
        auto fileNames = [&] {
            QStringList result;
            for (int row = 0; row < table(window)->rowCount(); ++row)
                if (!table(window)->model()->index(row, 0).data(Qt::UserRole + 1).toBool())
                    result << table(window)->model()->index(row, 0).data().toString();
            return result;
        };
        // The source Agent comparator uses natural ordering for flat prefixes.
        const QStringList expected = {"dir2/common.txt", "dir10/common.txt", "z/file2.txt",
                                      "a/file10.txt", QStringLiteral("папка с пробелом/Альфа.txt")};
        QCOMPARE(fileNames(), expected);
        action(window, "sort3Action")->trigger();
        QStringList reversed = expected;
        std::reverse(reversed.begin(), reversed.end());
        QCOMPARE(fileNames(), reversed);
        table(window)->selectRow(rowWithPath(window, "dir2/common.txt"));
        action(window, "sort3Action")->trigger();
        QCOMPARE(fileNames(), expected);
        window.show();
        const QString screenshots = qEnvironmentVariable("SEVENZIP_SCREENSHOT_DIR");
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*table(window), window));
            QVERIFY(UiInspection::writeReport(
                window, QStringLiteral("source-sort-flat-%1").arg(size.width())));
        }
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QCoreApplication::processEvents();
                QVERIFY(window.grab().save(screenshots +
                                           QStringLiteral("/flat-sort-%1.png").arg(size.width())));
            }
        }
        window.extractTo(temp.path(), true);
        QCOMPARE(backend.lastSelected.size(), 1);
        QCOMPARE(QDir::fromNativeSeparators(backend.lastSelected.first()),
                 QString("dir2/common.txt"));
        backend.cancel();
    }

    void extensionUsesLastDotIncludingDotfilesAndFolders() {
        QTemporaryDir temp;
        ControlledBackend backend;
        MainWindow window(&backend);
        openListedArchive(window, backend, temp.filePath("extensions.7z"),
                          {{".hidden", 1, 1},
                           {"README", 1, 1},
                           {"last.", 1, 1},
                           {"a.tar.gz", 1, 1},
                           {"b.TXT", 1, 1},
                           {"z.aaa", 0, 0, {}, true},
                           {"a.zzz", 0, 0, {}, true}});
        action(window, "sort1Action")->trigger();
        QCOMPARE(names(window), (QStringList{"z.aaa", "a.zzz", "README", "last.", "a.tar.gz",
                                             ".hidden", "b.TXT"}));
        action(window, "sort1Action")->trigger();
        QCOMPARE(names(window), (QStringList{"a.zzz", "z.aaa", "b.TXT", ".hidden", "a.tar.gz",
                                             "last.", "README"}));
    }

    void flatFilesystemUsesBaseNames() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir("a"));
        QVERIFY(QDir(temp.path()).mkdir("z"));
        for (const QString &name : {"a/file10.txt", "z/file2.txt"}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("same size");
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "flatViewAction")->setChecked(true);
        QTRY_VERIFY(action(window, "sort3Action")->isEnabled());
        QCOMPARE(names(window).mid(2), (QStringList{"z/file2.txt", "a/file10.txt"}));
        action(window, "sort3Action")->trigger();
        QCOMPARE(names(window).mid(2), (QStringList{"a/file10.txt", "z/file2.txt"}));
        action(window, "flatViewAction")->setChecked(false);
    }

    void archiveDirectionGroupingAndRefresh() {
        QTemporaryDir temp;
        ControlledBackend backend;
        MainWindow window(&backend);
        const QList<ArchiveEntry> entries = {{"file2.txt", 2, 20, "2022-01-01"},
                                             {"file10.txt", 10, 10, "2024-01-01"},
                                             {QStringLiteral("папка с пробелом"), 0, 0, {}, true},
                                             {"folder", 0, 0, {}, true}};
        openListedArchive(window, backend, temp.filePath("sort.7z"), entries);
        const QStringList folders = {"folder", QStringLiteral("папка с пробелом")};
        action(window, "sort3Action")->trigger();
        QCOMPARE(names(window), (QStringList{folders[1], folders[0], "file10.txt", "file2.txt"}));
        QCOMPARE(table(window)->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        backend.queueList(entries);
        action(window, "refreshAction")->trigger();
        QCOMPARE(names(window), (QStringList{folders[1], folders[0], "file10.txt", "file2.txt"}));
        action(window, "sort3Action")->trigger();
        QCOMPARE(names(window), (QStringList{folders[0], folders[1], "file2.txt", "file10.txt"}));
        // A header property not exposed by the sort menu must survive refresh too.
        table(window)->sortItems(3, Qt::DescendingOrder);
        QCOMPARE(names(window).mid(2), (QStringList{"file2.txt", "file10.txt"}));
        backend.queueList(entries);
        action(window, "refreshAction")->trigger();
        QCOMPARE(table(window)->horizontalHeader()->sortIndicatorSection(), 3);
        QCOMPARE(table(window)->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(names(window).mid(2), (QStringList{"file2.txt", "file10.txt"}));
        action(window, "sort2Action")->trigger();
        QCOMPARE(names(window).mid(2), (QStringList{"file10.txt", "file2.txt"}));
        action(window, "sort0Action")->trigger();
        action(window, "sort0Action")->trigger();
        QCOMPARE(names(window), (QStringList{folders[1], folders[0], "file10.txt", "file2.txt"}));
    }

    void headerDefaultsAndFilesystemNavigation() {
        QTemporaryDir temp;
        QVERIFY(QDir(temp.path()).mkdir(QStringLiteral("папка с пробелом")));
        for (const QString &name : {"file2.txt", "file10.txt"}) {
            QFile file(temp.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name == "file2.txt" ? "xx" : "xxxxxxxxxx");
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        auto *header = table(window)->horizontalHeader();
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(header->sectionViewportPosition(2) + 10, header->height() / 2));
        QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(names(window),
                 (QStringList{QStringLiteral("папка с пробелом"), "file10.txt", "file2.txt"}));
        action(window, "refreshAction")->trigger();
        QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(names(window).mid(1), (QStringList{"file10.txt", "file2.txt"}));
        address->setText(temp.filePath(QStringLiteral("папка с пробелом")));
        QTest::keyClick(address, Qt::Key_Return);
        action(window, "parentFolderAction")->trigger();
        QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(names(window).mid(1), (QStringList{"file10.txt", "file2.txt"}));
        const QString screenshots = qEnvironmentVariable("SEVENZIP_SCREENSHOT_DIR");
        if (!screenshots.isEmpty()) {
            QDir().mkpath(screenshots);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QCoreApplication::processEvents();
                QVERIFY(window.grab().save(screenshots +
                                           QStringLiteral("/sort-%1.png").arg(size.width())));
            }
        }
    }
};
QTEST_MAIN(PanelSortTests)
#include "panel_sort_tests.moc"
