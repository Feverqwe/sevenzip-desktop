#include "archive/agent/Agent.h"
#include "archive/diagnostics/archiveerrormessages.h"
#include "archive/resources/qtarchivefileresource.h"
#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "support/window_activation.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/formatting/qtregionalformat.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

class PropertiesWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QtPropertyReport collect(IFolderFolder *folder, std::initializer_list<UInt32> selected) {
        CPanel panel(folder);
        CRecordVector<UInt32> indices;
        for (auto index : selected)
            indices.Add(index);
        QtPropertyReport result;
        panel.Properties(indices, result);
        return result;
    }
    static QString shownSize(const QtPropertiesWindow &window) {
        return window.findChild<QLabel *>("propertiesSize")->text();
    }
    static void copy(QtPropertiesWindow &window) {
        window.findChild<QPushButton *>("propertiesCopyInformation")->click();
    }
    static bool readableControls(const QtPropertiesWindow &window) {
        for (auto *label : window.findChildren<QLabel *>())
            if (label->isVisible() && !label->text().isEmpty() && !UiInspection::labelFits(*label))
                return false;
        for (auto *button : window.findChildren<QAbstractButton *>())
            if (button->isVisible() && button->width() < button->sizeHint().width())
                return false;
        auto *actions = window.findChild<QDialogButtonBox *>("propertiesActions");
        for (auto *button : actions->buttons())
            if (!UiInspection::fullyVisible(*button, window))
                return false;
        return true;
    }
  private slots:
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
    }
    void fullCopyKeepsOwnedArchiveLevelsAndUnopenedDiagnostics() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("outer metadata.7z");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("fixture");
        file.close();
        const QString comment = QStringLiteral("full й 😀 & <comment>\n").repeated(150);
        QtPropertyReport report;
        {
            auto *resource = new QtArchiveFileResource(path);
            CMyComPtr<IUnknown> owner = resource;
            resource->Info.format = "7z";
            resource->Info.physicalSize = 17000;
            resource->Info.properties.insert("Comment", comment);
            ArchiveEntry entry{"selected.txt", 7, 3};
            entry.rawProperties.insert(kpidCRC, QByteArray::fromHex("deadbeef"));
            CMyComPtr<IFolderFolder> folder;
            QCOMPARE(CreateArchiveFolder({entry}, UString("7z"), &folder, owner), S_OK);
            CMyComPtr<IGetFolderArcProps> arcOwner;
            QCOMPARE(folder.QueryInterface(IID_IGetFolderArcProps, &arcOwner), S_OK);
            CMyComPtr<IFolderArcProps> levels;
            QCOMPARE(arcOwner->GetFolderArcProps(&levels), S_OK);
            auto *agent = dynamic_cast<CAgent *>((IFolderArcProps *)levels);
            QVERIFY(agent);
            auto &inner = agent->_archiveLink.Arcs.AddNew();
            inner.Archive = agent->_archiveLink.Arcs[0].Archive;
            QCOMPARE(resource->ConfigureArc(inner), S_OK);
            inner.Path = L"inner.7z";
            inner.SubfileIndex = 0;
            agent->_archiveLink.NonOpen_ArcPath = L"rejected.child";
            agent->_archiveLink.NonOpen_ErrorInfo.ErrorFlags = kpv_ErrorFlags_CrcError;
            report = collect(folder, {0});
        }
        QVERIFY(QFile::remove(path)); // No provider/reader is needed by the information window.
        QtPropertiesWindow window(std::move(report));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(readableControls(window));
        QVERIFY(UiInspection::writeReport(window, "properties-info-nested-en"));
        QCOMPARE(shownSize(window), QtRegionalFormat::bytes(7));
        for (auto *section : window.findChildren<QtDisclosure *>())
            QVERIFY(section->isExpanded());
        copy(window);
        const QString original = QApplication::clipboard()->text();
        QVERIFY(original.contains(comment));
        QVERIFY(original.contains("CRC (stored metadata): DEADBEEF"));
        QVERIFY(original.contains("Archive 2\nPath: inner.7z"));
        QVERIFY(original.contains("Archive 1\nPath: " + path));
        QVERIFY(original.contains("rejected.child"));
        QVERIFY(original.contains(ToQString(GetOpenArcErrorMessage(kpv_ErrorFlags_CrcError))));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        copy(window);
        const QString russian = QApplication::clipboard()->text();
        QVERIFY(russian.contains(comment));
        QVERIFY(russian.contains("CRC (из метаданных): DEADBEEF"));
        QVERIFY(russian != original);
        QTRY_VERIFY(readableControls(window));
        QVERIFY(UiInspection::writeReport(window, "properties-info-nested-ru"));
        for (auto *section : window.findChildren<QtDisclosure *>())
            QVERIFY(section->isExpanded());
        QCOMPARE(shownSize(window), QtRegionalFormat::bytes(7));
    }
    void unknownZeroFoldersAndPartialSelectionStayDistinct() {
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(
            CreateArchiveFolder(
                {{"unknown", -1, -1}, {"zero", 0, 0}, {"known", 11, 7}, {"folder/child", 13, 5}},
                UString("zip"), &folder),
            S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        std::optional<UInt32> unknown, zero, known, directory;
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant value;
            QCOMPARE(folder->GetProperty(i, kpidName, &value), S_OK);
            const QString name = QString::fromWCharArray(value.bstrVal);
            if (name == "unknown")
                unknown = i;
            else if (name == "zero")
                zero = i;
            else if (name == "known")
                known = i;
            else if (name == "folder")
                directory = i;
        }
        QVERIFY(unknown && zero && known && directory);
        QtPropertiesWindow missing(collect(folder, {*unknown}));
        QtPropertiesWindow empty(collect(folder, {*zero}));
        QtPropertiesWindow contents(collect(folder, {*directory}));
        QtPropertiesWindow multiple(collect(folder, {*unknown, *zero, *directory}));
        QtPropertiesWindow knownFiles(collect(folder, {*zero, *known}));
        QtPropertiesWindow knownFolderAndFile(collect(folder, {*directory, *known}));
        QCOMPARE(shownSize(missing), QString::fromUtf8("—"));
        QVERIFY(missing.findChild<QWidget *>("propertiesIdentity")->toolTip().isEmpty());
        QCOMPARE(shownSize(empty), QtRegionalFormat::bytes(0));
        QCOMPARE(shownSize(contents), QtRegionalFormat::bytes(13));
        QVERIFY(contents.Information().contains("Size: " + QtRegionalFormat::bytes(13)));
        QCOMPARE(shownSize(multiple), QString("Not calculated"));
        QCOMPARE(shownSize(knownFiles), QtRegionalFormat::bytes(11));
        QCOMPARE(shownSize(knownFolderAndFile), QtRegionalFormat::bytes(24));
        QCOMPARE(knownFolderAndFile.findChild<QLabel *>("propertiesSize")->toolTip(),
                 QtRegionalFormat::exactBytes(24));
        QVERIFY(multiple.findChild<QLabel *>("propertiesSize")->toolTip().isEmpty());
        copy(knownFolderAndFile);
        QVERIFY(QApplication::clipboard()->text().contains("Size: " + QtRegionalFormat::bytes(24)));
        copy(knownFiles);
        QVERIFY(QApplication::clipboard()->text().contains("Size: " + QtRegionalFormat::bytes(11)));
        QVERIFY(multiple.Information().contains("Files: 2 · Folders: 1"));
        QVERIFY(multiple.Information().contains("Reported size (may be partial)"));
        QCOMPARE(multiple.findChild<QLabel *>("propertiesModified")->text(),
                 QString("Not applicable"));
        const QString englishSelection = QtSelectedItemsText(3);
        copy(multiple);
        const QString englishReport = QApplication::clipboard()->text();
        QVERIFY(englishReport.contains(englishSelection));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString("ru"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        copy(multiple);
        const QString russianReport = QApplication::clipboard()->text();
        QVERIFY(!russianReport.contains(englishSelection));
        QVERIFY(russianReport.contains(QtSelectedItemsText(3)));
        for (const QString &name :
             {QStringLiteral("unknown"), QStringLiteral("zero"), QStringLiteral("folder")})
            QVERIFY(russianReport.split(u'\n').contains(name));
        SaveRegLang(UString("-"), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        copy(multiple);
        QCOMPARE(QApplication::clipboard()->text(), englishReport);
        for (const auto &[window, tag] :
             {std::pair{&missing, "unknown"}, std::pair{&empty, "zero"},
              std::pair{&contents, "folder"}, std::pair{&multiple, "multiple"}}) {
            window->resize(520, 360);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            if (window == &multiple) {
                for (auto *section : window->findChildren<QtDisclosure *>())
                    section->setExpanded(true);
                QLabel *caption = nullptr;
                for (auto *label : window->findChildren<QLabel *>())
                    if (label->text() == QStringLiteral("Reported size (may be partial)"))
                        caption = label;
                QVERIFY(caption && caption->isVisible());
                QTRY_VERIFY(UiInspection::labelFits(*caption));
            }
            QVERIFY(UiInspection::writeReport(*window, QString("properties-info-%1").arg(tag)));
            window->hide();
        }
    }
    void navigationDoesNotRetargetIndependentWindows_data() {
        QTest::addColumn<bool>("switchToIcons");
        QTest::newRow("details") << false;
        QTest::newRow("switch-to-icons") << true;
    }
    void navigationDoesNotRetargetIndependentWindows() {
        QFETCH(bool, switchToIcons);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, "original.zip", {{"first.txt", 7, 3}});
        table(window)->selectRow(0);
        table(window)->setFocus();
        action(window, "selectedPropertiesAction")->trigger();
        QPointer<QtPropertiesWindow> first = window.findChild<QtPropertiesWindow *>();
        QVERIFY(first && !first->isModal());
        const QString before = first->Information();
        openListedArchive(window, backend, "second.zip",
                          {{"second.txt", 19, 9}, {"z-next.txt", 23, 11}});
        table(window)->selectRow(0);
        window.findChild<QLineEdit *>("archivePath")->setFocus();
        action(window, "selectedPropertiesAction")->trigger();
        const auto windows = window.findChildren<QtPropertiesWindow *>();
        QCOMPARE(windows.size(), 2);
        QPointer<QtPropertiesWindow> second = windows.last();
        QVERIFY(second != first);
        QVERIFY(second->Information().contains("second.txt"));
        QCOMPARE(first->Information(), before);
        QWidget *focusTarget = table(window);
        if (switchToIcons) {
            action(window, "viewMode0Action")->trigger();
            focusTarget = window.findChild<QListView *>("iconList");
            QVERIFY(focusTarget && focusTarget->isVisible());
            QVERIFY(!table(window)->isVisible());
        }
        second->raise();
        second->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(second));
        QTest::keyClick(second, Qt::Key_Escape);
        QTRY_VERIFY(!second);
        QVERIFY(first && first->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), focusTarget);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_End);
        action(window, "selectedPropertiesAction")->trigger();
        const auto reopened = window.findChildren<QtPropertiesWindow *>();
        QCOMPARE(reopened.size(), 2);
        QVERIFY(reopened.last()->Information().contains("z-next.txt"));
        QCOMPARE(first->Information(), before);
        reopened.last()->close();
        ControlledBackend otherBackend;
        MainWindow other(&otherBackend);
        other.show();
        QVERIFY(WindowActivation::waitForActive(other));
        QWidget *otherView = table(other);
        if (!otherView->isVisible())
            otherView = other.findChild<QListView *>("iconList");
        QVERIFY(otherView && otherView->isVisible());
        otherView->setFocus();
        QTRY_VERIFY(otherView->hasFocus());
        first->close();
        QTRY_VERIFY(!first);
        QCOMPARE(QApplication::activeWindow(), static_cast<QWidget *>(&other));
        QCOMPARE(QApplication::focusWidget(), otherView);
        QCOMPARE(backend.extractCalls, 0);
    }
    void overflowingSourceTotalIsNotShownAsZero() {
        ArchiveEntry huge{"huge", -1, 0};
        huge.sizeValue = UInt64(-1);
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({huge, {"one", 1, 0}}, UString("zip"), &folder), S_OK);
        QtPropertiesWindow window(collect(folder, {0, 1}));
        QCOMPARE(shownSize(window), QString("Not calculated"));
        QVERIFY(window.Information().contains("Reported size (may be partial): Not available"));
        QVERIFY(window.Information().contains("Files: 2"));
    }
    void missingSelectionSizesAreDistinctFromZero() {
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({{"unknown-a", -1, -1}, {"unknown-b", -1, -1}, {"zero", 0, 0}},
                                     UString("zip"), &folder),
                 S_OK);
        QtPropertiesWindow missing(collect(folder, {0, 1}));
        QtPropertiesWindow partial(collect(folder, {0, 2}));
        const QString caption = QStringLiteral("Reported packed size (may be partial): ");
        copy(missing);
        QVERIFY(QApplication::clipboard()->text().contains(caption + "Not available"));
        QVERIFY(QApplication::clipboard()->text().contains(
            "Reported size (may be partial): Not available"));
        copy(partial);
        QVERIFY(
            QApplication::clipboard()->text().contains(caption + QtRegionalFormat::exactBytes(0)));
        QVERIFY(QApplication::clipboard()->text().contains("Reported size (may be partial): " +
                                                           QtRegionalFormat::exactBytes(0)));
    }
    void withoutSelectionUsesCurrentFolder() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, "current-folder.zip", {{"unselected.txt", 7, 3}});
        table(window)->clearSelection();
        QVERIFY(table(window)->selectionModel()->selectedRows().isEmpty());
        QVERIFY(action(window, "selectedPropertiesAction")->isEnabled());
        action(window, "selectedPropertiesAction")->trigger();
        auto *properties = window.findChild<QtPropertiesWindow *>();
        QVERIFY(properties);
        QVERIFY(QTest::qWaitForWindowExposed(properties));
        QCOMPARE(shownSize(*properties), QtRegionalFormat::bytes(7));
        QCOMPARE(properties->findChild<QWidget *>("propertiesIdentity")->accessibleName(),
                 QString("current-folder.zip"));
        QVERIFY(properties->findChild<QLabel *>("propertiesLocation")
                    ->text()
                    .contains("current-folder.zip"));
        QVERIFY(UiInspection::writeReport(*properties, "properties-info-current-folder"));
        QCOMPARE(backend.extractCalls, 0);
        properties->close();
    }
    void filesystemCommentUsesItsOwnDisclosure() {
        QTemporaryDir fixture;
        QFile input(fixture.filePath("comment.txt"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("data");
        input.close();
        const QString comment = QStringLiteral("native comment & <текст> й 😀");
        QFile descriptions(fixture.filePath("descript.ion"));
        QVERIFY(descriptions.open(QIODevice::WriteOnly));
        const QByteArray bytes =
            QByteArray::fromHex("efbbbf") + "comment.txt " + comment.toUtf8() + '\n';
        QCOMPARE(descriptions.write(bytes), qint64(bytes.size()));
        descriptions.close();
        CMyComPtr<IFolderFolder> folder = new NFsFolder::CFSFolder(fixture.path());
        QCOMPARE(folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(folder->GetNumberOfItems(&count), S_OK);
        std::optional<UInt32> selected;
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant name;
            QCOMPARE(folder->GetProperty(i, kpidName, &name), S_OK);
            if (ToQString(UString(name.bstrVal)) == "comment.txt")
                selected = i;
        }
        QVERIFY(selected);
        QtPropertiesWindow window(collect(folder, {*selected}));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCoreApplication::processEvents();
        QVERIFY(!window.isMaximized());
        QVERIFY(window.height() <= 440);
        QtDisclosure *comments = nullptr;
        for (auto *section : window.findChildren<QtDisclosure *>())
            if (section->button()->text() == "Comment")
                comments = section;
        QVERIFY(comments && comments->isExpanded());
        copy(window);
        QVERIFY(QApplication::clipboard()->text().contains(comment));
        QTRY_VERIFY(readableControls(window));
        QVERIFY(UiInspection::writeReport(window, "properties-info-comment"));
    }
    void geometry_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("frame");
        for (const QString &language : {QString("-"), QString("ru")})
            for (QSize frame : {QSize(540, 440), QSize(360, 280)})
                QTest::newRow(qPrintable(language + QString::number(frame.width())))
                    << language << frame;
    }
    void geometry() {
        QFETCH(QString, language);
        QFETCH(QSize, frame);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        QtNotifyLanguageChanged();
        CMyComPtr<IFolderFolder> folder;
        const QString name = QStringLiteral("длинное имя & <текст> 😀 ").repeated(5) + ".txt";
        QCOMPARE(CreateArchiveFolder({{name, 12345, 5432}}, UString("zip"), &folder), S_OK);
        auto report = collect(folder, {0});
        report.location = "/" + QStringLiteral("long path й 空/").repeated(25);
        QtPropertiesWindow window(std::move(report));
        window.resize(frame);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCoreApplication::processEvents();
        // Native button metrics can impose a wider minimum for translated text.
        QCOMPARE(window.size(), frame.expandedTo(window.minimumSize()));
        auto *body = window.findChild<QScrollArea *>();
        auto *actions = window.findChild<QDialogButtonBox *>("propertiesActions");
        QVERIFY(body && actions);
        const auto inspect = [&] {
            return UiInspection::fullyVisible(*actions, window) &&
                   UiInspection::fullyVisible(*body, window) &&
                   UiInspection::separate(*body, *actions, window);
        };
        QTRY_VERIFY(inspect());
        for (auto *button : actions->buttons())
            QVERIFY(UiInspection::fullyVisible(*button, window));
        const QString tag = QString("properties-info-%1-%2").arg(language).arg(frame.width());
        QVERIFY(UiInspection::writeReport(window, tag));
        QTRY_VERIFY(inspect());
        QTRY_COMPARE(body->horizontalScrollBar()->maximum(), 0);
        for (auto *label : window.findChildren<QLabel *>())
            if (label->isVisible() && label->wordWrap() && !label->text().isEmpty())
                QTRY_VERIFY2(UiInspection::labelFits(*label), qPrintable(label->objectName()));
        QVERIFY(body->verticalScrollBar()->maximum() > 0);
        QVERIFY(UiInspection::writeReport(window, tag + "-expanded"));
        copy(window);
        QVERIFY(QApplication::clipboard()->text().contains(name));
        QVERIFY(QApplication::clipboard()->text().contains(
            QStringLiteral("long path й 空/").repeated(25)));
        auto *identity = window.findChild<QWidget *>("propertiesIdentity");
        QLabel *heading = nullptr;
        for (auto *label : identity->findChildren<QLabel *>())
            if (label->text() == name)
                heading = label;
        QVERIFY(heading);
        heading->setSelection(2, 8);
        const auto shortcut = QKeySequence(QKeySequence::Copy)[0];
        QTest::keyClick(heading, shortcut.key(), shortcut.keyboardModifiers());
        QCOMPARE(QApplication::clipboard()->text(), name.mid(2, 8));
        copy(window);
        QVERIFY(QApplication::clipboard()->text().contains(name));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PropertiesWindowTests)
#include "properties_window_tests.moc"
