#include "7zip/UI/FileManager/resource.h"
#include "filemanager/SysIconUtils.h"
#include "filemanager/panels/qtpaneladdresscombo.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/support/external_editor_fixture.h"
#include "support/native_shell_icon.h"
#include "support/panel_keys.h"
#include "support/window_activation.h"
#include "ui/support/address_popup_support.h"
#include "ui/support/layout_inspection.h"
#include <QFileIconProvider>
#include <QStyle>
#include <QToolButton>

class AddressComboTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
#ifdef Q_OS_WIN
    void shellPathIconUsesRequestedAttributes() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("icon файл 空.txt"));
        QVERIFY(ExternalEditorFixture::write(path, "unchanged"));
        CExtToIconMap icons;
        for (DWORD attributes : {DWORD(FILE_ATTRIBUTE_DIRECTORY), DWORD(FILE_ATTRIBUTE_ARCHIVE)}) {
            const auto expected = NativeShellIcon::image(path, attributes);
            QVERIFY(!expected.isNull());
            const int index = icons.GetPathIconIndex(ToUString(path), attributes);
            QCOMPARE(icons.GetIcon(index).pixmap(16, 16).toImage(), expected);
            QCOMPARE(icons.GetPathIconIndex(ToUString(path), attributes), index);
        }
        QCOMPARE(ExternalEditorFixture::read(path), QByteArray("unchanged"));
    }
#endif
    void currentIconPreservesAlternateStreamColon_data() {
        QTest::addColumn<QString>("delimiter");
        QTest::newRow("archive-directory-separator") << QString("/");
        QTest::newRow("alternate-stream-colon") << QString(":");
    }
    void currentIconPreservesAlternateStreamColon() {
        QFETCH(QString, delimiter);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString path = fixture.filePath(QStringLiteral("base файл 空.txt"));
        QVERIFY(ExternalEditorFixture::write(path, "unchanged"));
        CObjectVector<CPanelItem> items;
        CPanel panel(path + delimiter, items);
        QtPanelAddressCombo combo(panel, [](const QString &, bool) { return S_OK; }, nullptr);
        combo.ShowCurrentPath(path + delimiter);
        auto *current = combo.lineEdit()->findChild<QAction *>("currentAddressIcon");
        QVERIFY(current);
        QVERIFY(current->isVisible());
        QVERIFY(!current->icon().isNull());
#ifdef Q_OS_WIN
        const QImage expected = NativeShellIcon::image(delimiter == ":" ? path + delimiter : path,
                                                       delimiter == ":" ? FILE_ATTRIBUTE_DIRECTORY
                                                                        : FILE_ATTRIBUTE_ARCHIVE);
#else
        const QIcon expectedIcon = delimiter == ":"
                                       ? QFileIconProvider().icon(QFileIconProvider::Folder)
                                       : QFileIconProvider().icon(QFileInfo(path));
        const QImage expected = expectedIcon.pixmap(16, 16).toImage();
#endif
        QVERIFY(!expected.isNull());
        QCOMPARE(current->icon().pixmap(16, 16).toImage(), expected);
        QCOMPARE(combo.currentText(), path + delimiter);
        QCOMPARE(ExternalEditorFixture::read(path), QByteArray("unchanged"));
    }
    void init() {
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        ReloadLang(settings);
    }
    void matchingHierarchyCaptionDoesNotNavigateOnBlur() {
        QTemporaryDir fixture;
        const QString child = fixture.filePath("kept location");
        QVERIFY(QDir().mkpath(child));
        QVERIFY(ExternalEditorFixture::write(child + "/keep.txt", "unchanged"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openPath(child);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled());
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QTRY_VERIFY(combo->view()->isVisible());
        combo->hidePopup();
        QVERIFY(WindowActivation::waitForActive(window));
        PanelKeys::focusAddress(address);
        QVERIFY(combo->count() > 0);
        const QString draft = combo->itemText(0);
        address->setText(draft);
        QSignalSpy activated(combo, &QComboBox::activated);
        files->setFocus();
        QTRY_VERIFY(files->hasFocus());
        QApplication::processEvents();
        QCOMPARE(activated.size(), 0);
        QCOMPARE(
            QFileInfo(ToQString(files->sourcePanel->GetCurrentFolderPrefix())).canonicalFilePath(),
            QFileInfo(child).canonicalFilePath());
        QCOMPARE(files->rowCount(), 1);
        QCOMPARE(files->model()->index(0, 0).data().toString(), QString("keep.txt"));
        QCOMPARE(backend.listCalls, 0);
    }
    void decorativeIconAllowsPaddingClipButRejectsGlyphClip() {
        QWidget window;
        window.resize(320, 80);
        QLineEdit edit(&window);
        edit.setFrame(false);
        const int size = edit.style()->pixelMetric(QStyle::PM_LineEditIconSize, nullptr, &edit);
        QVERIFY(size > 1);
        edit.setGeometry(10, 10, 280, size + 1);
        edit.addAction(edit.style()->standardIcon(QStyle::SP_DirIcon), QLineEdit::LeadingPosition);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *button = edit.findChild<QToolButton *>();
        QVERIFY(button);
        QVERIFY(!UiInspection::fullyVisible(*button, window));
        QVERIFY(UiInspection::lineEditIconVisible(*button, window));
        edit.resize(edit.width(), size - 1);
        QApplication::processEvents();
        QVERIFY(!UiInspection::lineEditIconVisible(*button, window));
        edit.resize(edit.width(), size + 1);
        edit.hide();
        QVERIFY(!UiInspection::lineEditIconVisible(*button, window));
    }
    void queuedSelectionFocusWaitsForPopupClosure() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString child = fixture.filePath(QStringLiteral("внутри space"));
        QVERIFY(QDir().mkpath(child));
        QFile file(fixture.filePath(QStringLiteral("keep 空.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("preserved"), qint64(9));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(child);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(table(window)->isEnabled());
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        auto *view = combo->view();
        QVERIFY(QTest::qWaitForWindowExposed(view->window()));
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        const int index = combo->findText(QFileInfo(fixture.path()).fileName());
        QVERIFY(index >= 0);
        combo->setCurrentIndex(index);
        // Source navigation can deliver posted events inside activated(),
        // before QComboBox subsequently hides its selection popup.
        QVERIFY(
            QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection, Q_ARG(int, index)));
        QApplication::processEvents();
        QVERIFY(view->isVisible());
        QCOMPARE(QDir::cleanPath(address->text()), QFileInfo(fixture.path()).canonicalFilePath());
        combo->hidePopup();
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(table(window)->hasFocus());
        QTRY_COMPARE(table(window)->rowCount(), 2);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("preserved"));
        QCOMPARE(backend.listCalls, 0);
    }
    void dropdownContainsCurrentHierarchy_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const char *language : {"-", "ru"})
            for (const QSize size : {QSize(980, 620), QSize(640, 400)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(size.width())))
                    << QString::fromLatin1(language) << size;
    }
    void dropdownContainsCurrentHierarchy() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString parent = fixture.filePath(QStringLiteral("родитель space"));
        const QString child = parent + QStringLiteral("/внутри 密碼");
        QVERIFY(QDir().mkpath(child));
        QFile payload(child + "/keep.txt");
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("unchanged"), 9);
        payload.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(child);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QVERIFY(combo);
        auto *currentIcon = address->findChild<QAction *>("currentAddressIcon");
        QVERIFY2(currentIcon, "The address editor must expose its current-path icon.");
        PanelKeys::focusAddress(address);
        QVERIFY(currentIcon->isVisible());
        QVERIFY(!currentIcon->icon().isNull());
        QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                 QFileIconProvider().icon(QFileInfo(child)).pixmap(16, 16).toImage());
        const auto iconButtons = address->findChildren<QToolButton *>();
        QVERIFY(!iconButtons.isEmpty());
        for (const auto *button : iconButtons) {
            if (!UiInspection::lineEditIconVisible(*button, window))
                QVERIFY(UiInspection::writeReport(
                    window, QString("address-icon-clipped-%1-%2").arg(language).arg(size.width())));
            QVERIFY(UiInspection::lineEditIconVisible(*button, window));
        }
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        QVERIFY2(combo->findText(QStringLiteral("родитель space")) >= 0,
                 "The source dropdown must include the current path's parent component.");
        QVERIFY(combo->findText(QStringLiteral("внутри 密碼")) >= 0);
        QVERIFY(combo->findText(QString::fromWCharArray(LangString(IDS_COMPUTER))) >= 0);
        QCOMPARE(combo->currentText(), address->text());
        QVERIFY(UiInspection::fullyVisible(*combo, window));
        QVERIFY(UiInspection::fullyVisible(*table(window), window));
        QVERIFY(UiInspection::writeReport(
            window, QString("address-combo-%1-%2").arg(language).arg(size.width())));
        const int row = combo->findText(QStringLiteral("родитель space"));
        AddressPopupSupport::selectRow(*combo, row);
        QVERIFY(!QTest::currentTestFailed());
        QTRY_COMPARE(QDir::cleanPath(address->text()), QFileInfo(parent).canonicalFilePath());
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(),
                 QStringLiteral("внутри 密碼"));
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        const int computer = combo->findText(QString::fromWCharArray(LangString(IDS_COMPUTER)));
        QVERIFY(computer >= 0);
        AddressPopupSupport::selectRow(*combo, computer);
        QVERIFY(!QTest::currentTestFailed());
#ifdef Q_OS_WIN
        QVERIFY(!action(window, "createFolderAction")->isEnabled());
#else
        QTRY_COMPARE(address->text(), QStringLiteral("/"));
#endif
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("unchanged"));
        QCOMPARE(backend.listCalls, 0);
    }
    void archiveHierarchySelectionPreservesArchive_data() {
        QTest::addColumn<bool>("nested");
        QTest::newRow("single") << false;
        QTest::newRow("nested") << true;
    }
    void archiveHierarchySelectionPreservesArchive() {
        QFETCH(bool, nested);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString entry = QStringLiteral("внешняя space/内側 密碼/keep.txt");
        QVERIFY(QDir().mkpath(QFileInfo(fixture.filePath(entry)).absolutePath()));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(entry), "unchanged"));
        const QString archive = fixture.filePath(QStringLiteral("архив δ.zip"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, entry));
        const QByteArray original = ExternalEditorFixture::read(archive);
        const QString outer = fixture.filePath(QStringLiteral("outer space.zip"));
        QByteArray originalOuter;
        if (nested) {
            QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), outer,
                                                         QFileInfo(archive).fileName()));
            originalOuter = ExternalEditorFixture::read(outer);
        }
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        window.openArchive(nested ? outer : archive);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QVERIFY(address && combo);
        QTRY_VERIFY(action(window, "parentFolderAction")->isEnabled());
        if (nested) {
            QTRY_COMPARE(table(window)->rowCount(), 1);
            QTRY_COMPARE(table(window)->model()->index(0, 0).data().toString(),
                         QFileInfo(archive).fileName());
            openRow(window, 0);
            QTRY_COMPARE(table(window)->model()->index(0, 0).data().toString(),
                         QStringLiteral("внешняя space"));
        }
        const QString archiveAddress = address->text();
        auto *currentIcon = address->findChild<QAction *>("currentAddressIcon");
        QVERIFY(currentIcon);
        QVERIFY(!currentIcon->icon().isNull());
        if (!nested)
            QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                     QFileIconProvider().icon(QFileInfo(archive)).pixmap(16, 16).toImage());
        PanelKeys::focusAddress(address);
        address->setText(archiveAddress + QStringLiteral(" / внешняя space/内側 密碼"));
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(), QStringLiteral("keep.txt"));
        QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                 QFileIconProvider().icon(QFileIconProvider::Folder).pixmap(16, 16).toImage());
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        QVERIFY(combo->findText(QStringLiteral("архив δ.zip")) >= 0);
        const int parent = combo->findText(QStringLiteral("внешняя space"));
        QVERIFY(parent >= 0);
        AddressPopupSupport::selectRow(*combo, parent);
        QVERIFY(!QTest::currentTestFailed());
        QTRY_COMPARE(address->text(), archiveAddress + QStringLiteral(" / внешняя space"));
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(),
                 QStringLiteral("内側 密碼"));
        QCOMPARE(ExternalEditorFixture::read(archive), original);
        if (nested)
            QCOMPARE(ExternalEditorFixture::read(outer), originalOuter);
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(entry)), QByteArray("unchanged"));
        action(window, "parentFolderAction")->trigger();
        QTRY_COMPARE(address->text(), archiveAddress);
        QFileIconProvider provider;
        QIcon archiveIcon =
            provider.icon(QFileInfo(nested ? fixture.filePath("missing.zip") : archive));
        if (archiveIcon.isNull())
            archiveIcon = provider.icon(QFileIconProvider::File);
        QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                 archiveIcon.pixmap(16, 16).toImage());
        action(window, "parentFolderAction")->trigger();
        if (nested) {
            QTRY_COMPARE(address->text(), outer);
            QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                     QFileIconProvider().icon(QFileInfo(outer)).pixmap(16, 16).toImage());
        } else {
            QTRY_COMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
            QCOMPARE(currentIcon->icon().pixmap(16, 16).toImage(),
                     QFileIconProvider().icon(QFileInfo(fixture.path())).pixmap(16, 16).toImage());
        }
    }
    void staleHierarchySelectionUsesDeepestExistingFolder_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("keyboard");
        for (const char *language : {"-", "ru"})
            for (bool keyboard : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(keyboard)))
                    << QString::fromLatin1(language) << keyboard;
    }
    void staleHierarchySelectionUsesDeepestExistingFolder() {
        QFETCH(QString, language);
        QFETCH(bool, keyboard);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString parent = fixture.filePath(QStringLiteral("исчезающий parent 密碼"));
        const QString child = parent + QStringLiteral("/внутри space");
        QVERIFY(QDir().mkpath(child));
        const QString control = fixture.filePath(QStringLiteral("контроль δ.bin"));
        const QByteArray bytes("unchanged\0binary", 16);
        QVERIFY(ExternalEditorFixture::write(control, bytes));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        table(window)->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *combo = window.findChild<QComboBox *>("pathCombo");
        QVERIFY(address && combo);
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        action(window, "viewMode3Action")->trigger();
        address->setText(child);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(table(window)->hasFocus());
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        const int index = combo->findText(QFileInfo(parent).fileName());
        QVERIFY(index >= 0);
        // Change only the owned empty directories after the source popup snapshot.
        QVERIFY(QDir().rmdir(child));
        QVERIFY(QDir().rmdir(parent));
        QVERIFY(!QFileInfo::exists(parent));
        if (keyboard) {
            auto *popup = combo->view()->window();
            QVERIFY(QTest::qWaitForWindowExposed(popup));
            QTRY_VERIFY(combo->view()->hasFocus());
            combo->view()->setCurrentIndex(combo->view()->model()->index(index, 0));
            QTest::keyClick(popup->windowHandle(), Qt::Key_Return);
        } else
            AddressPopupSupport::selectRow(*combo, index);
        QVERIFY(!QTest::currentTestFailed());
        // Offscreen popup closure drops window activation. Re-activate only the
        // test window; do not supply the list focus that the source handler owns.
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_COMPARE(address->text(), QFileInfo(fixture.path()).canonicalFilePath());
        QTRY_VERIFY2(
            table(window)->hasFocus(),
            qPrintable(QString("focus=%1 active=%2 tableEnabled=%3")
                           .arg(QApplication::focusWidget()
                                    ? QApplication::focusWidget()->objectName()
                                    : QStringLiteral("none"))
                           .arg(QApplication::activeWindow()
                                    ? QApplication::activeWindow()->metaObject()->className()
                                    : "none")
                           .arg(table(window)->isEnabled())));
        QCOMPARE(table(window)->rowCount(), 1);
        QCOMPARE(table(window)->model()->index(0, 0).data().toString(),
                 QFileInfo(control).fileName());
        QVERIFY(action(window, "createFolderAction")->isEnabled());
        QCOMPARE(ExternalEditorFixture::read(control), bytes);
        QCOMPARE(backend.listCalls, 0);
        PanelKeys::focusAddress(address);
        AddressPopupSupport::show(*combo);
        QApplication::processEvents();
        QCOMPARE(combo->findText(QFileInfo(parent).fileName()), -1);
        combo->hidePopup();
    }
    void eachPanelUsesItsOwnHierarchyAndEscapeRestoresAddress() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString leftPath = fixture.filePath(QStringLiteral("left space/левый"));
        const QString rightPath = fixture.filePath(QStringLiteral("right space/右"));
        QVERIFY(QDir().mkpath(leftPath));
        QVERIFY(QDir().mkpath(rightPath));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        QWidget *panels[] = {window.findChild<QWidget *>("filePanel0"),
                             window.findChild<QWidget *>("filePanel1")};
        const QString paths[] = {leftPath, rightPath};
        for (unsigned i = 0; i < 2; ++i) {
            QVERIFY(panels[i]);
            auto *address = panels[i]->findChild<QLineEdit *>("archivePath");
            auto *view = panels[i]->findChild<FileTable *>("archiveTable");
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            action(window, "viewMode3Action")->trigger();
            address->setText(paths[i]);
            QTest::keyClick(address, Qt::Key_Return);
            QTRY_VERIFY(view->isEnabled());
            QTRY_COMPARE(QDir::cleanPath(address->text()), QFileInfo(paths[i]).canonicalFilePath());
            QTRY_VERIFY2(view->hasFocus(),
                         qPrintable(QString("panel=%1 visible=%2 focus=%3")
                                        .arg(i)
                                        .arg(view->isVisible())
                                        .arg(QApplication::focusWidget()
                                                 ? QApplication::focusWidget()->objectName()
                                                 : QStringLiteral("none"))));
        }
        for (unsigned i = 0; i < 2; ++i) {
            auto *combo = panels[i]->findChild<QComboBox *>("pathCombo");
            auto *address = panels[i]->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            PanelKeys::focusAddress(address);
            AddressPopupSupport::show(*combo);
            QApplication::processEvents();
            QStringList rows;
            for (int row = 0; row < combo->count(); ++row)
                rows.append(combo->itemText(row));
            QVERIFY2(combo->findText(i == 0 ? QStringLiteral("левый") : QStringLiteral("右")) >= 0,
                     qPrintable(QString("panel=%1 address=%2 rows=%3")
                                    .arg(i)
                                    .arg(address->text(), rows.join(" | "))));
            QVERIFY(combo->findText(i == 0 ? QStringLiteral("右") : QStringLiteral("левый")) < 0);
            combo->hidePopup();
            QVERIFY(WindowActivation::waitForActive(window));
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            address->setText(QStringLiteral("uncommitted address"));
            QTest::keyClick(address, Qt::Key_Escape);
            QCOMPARE(QDir::cleanPath(address->text()), QFileInfo(paths[i]).canonicalFilePath());
            QTRY_VERIFY(panels[i]->findChild<FileTable *>("archiveTable")->hasFocus());
        }
        QCOMPARE(backend.listCalls, 0);
    }
};

QTEST_MAIN(AddressComboTests)
#include "address_combo_tests.moc"
