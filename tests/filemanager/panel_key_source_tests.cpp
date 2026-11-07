#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class PanelKeySourceTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;

    static bool rightControlDigit(QWidget *view, bool shift, int digit) {
#ifdef Q_OS_WIN
        // Exercise native virtual digits and right-Control delivery, with all
        // keys released by one SendInput batch on the owned active test window.
        INPUT keys[6] = {};
        const WORD codes[] = {
            VK_RCONTROL, VK_SHIFT,   static_cast<WORD>(digit), static_cast<WORD>(digit),
            VK_SHIFT,    VK_RCONTROL};
        int count = 0;
        for (int i = 0; i < 6; ++i) {
            if (!shift && codes[i] == VK_SHIFT)
                continue;
            auto &input = keys[count++];
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = codes[i];
            input.ki.dwFlags = i >= 3 ? KEYEVENTF_KEYUP : 0;
            if (codes[i] == VK_RCONTROL)
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        Q_UNUSED(view);
        const bool sent = SendInput(UINT(count), keys, sizeof(INPUT)) == UINT(count);
        if (!sent) {
            INPUT release[3] = {};
            const WORD codesToRelease[] = {static_cast<WORD>(digit), VK_SHIFT, VK_RCONTROL};
            for (int i = 0; i < 3; ++i) {
                release[i].type = INPUT_KEYBOARD;
                release[i].ki.wVk = codesToRelease[i];
                release[i].ki.dwFlags = KEYEVENTF_KEYUP;
                if (codesToRelease[i] == VK_RCONTROL)
                    release[i].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }
            SendInput(3, release, sizeof(INPUT));
        }
        return sent;
#else
#ifdef Q_OS_MACOS
        constexpr quint32 nativeRightControl = 0x36;
#else
        constexpr quint32 nativeRightControl = 0xffe4;
#endif
        // Public Qt native-key notification protocol; this does not claim
        // physical Cocoa/XCB keyboard delivery.
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 0,
                       nativeRightControl, 0);
        QApplication::sendEvent(view, &down);
        QTest::keyClick(view, static_cast<Qt::Key>(digit),
                        shift ? Qt::ControlModifier | Qt::ShiftModifier : Qt::ControlModifier);
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier, 0, nativeRightControl, 0);
        QApplication::sendEvent(view, &up);
        return true;
#endif
    }

    static bool nativeDigitNotification(QWidget *view, bool shift, int digit) {
        const int index = digit - Qt::Key_0;
        const int logical = shift ? QStringLiteral(")!@#$%^&*(").at(index).unicode() : digit;
        const auto modifiers =
            shift ? Qt::ControlModifier | Qt::ShiftModifier : Qt::ControlModifier;
#ifdef Q_OS_WIN
        const quint32 controlScan = 0xe000, controlKey = VK_CONTROL;
        const quint32 scan = 0, nativeKey = digit, nativeModifiers = shift ? 0x21 : 0x20;
#elif defined(Q_OS_MACOS)
        const quint32 controlScan = 0, controlKey = 0x36;
        constexpr quint32 codes[] = {0x1d, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1a, 0x1c, 0x19};
        const quint32 scan = 0, nativeKey = codes[index];
        const quint32 nativeModifiers = (1u << 20) | (shift ? 1u << 17 : 0);
#else
        const quint32 controlScan = 105, controlKey = 0xffe4;
        const quint32 scan = index ? index + 9 : 19, nativeKey = logical;
        const quint32 nativeModifiers = shift ? 5 : 4;
#endif
        // Recorded US-layout native notification protocol, not OS input injection.
        QKeyEvent controlDown(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, controlScan,
                              controlKey, 0);
        QApplication::sendEvent(view, &controlDown);
        QKeyEvent override(QEvent::ShortcutOverride, logical, modifiers, scan, nativeKey,
                           nativeModifiers);
        override.ignore();
        QApplication::sendEvent(view, &override);
        QKeyEvent down(QEvent::KeyPress, logical, modifiers, scan, nativeKey, nativeModifiers);
        QApplication::sendEvent(view, &down);
        QKeyEvent up(QEvent::KeyRelease, logical, modifiers, scan, nativeKey, nativeModifiers);
        QApplication::sendEvent(view, &up);
        QKeyEvent controlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier, controlScan,
                            controlKey, 0);
        QApplication::sendEvent(view, &controlUp);
        return override.isAccepted();
    }
  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QApplication::setQuitOnLastWindowClosed(false);
    }
    void originalKeys_data() {
        QTest::addColumn<int>("key");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("copy-selected-files")
            << int(Qt::Key_C) << Qt::KeyboardModifiers(Qt::ControlModifier);
        QTest::newRow("select-all") << int(Qt::Key_A) << Qt::KeyboardModifiers(Qt::ControlModifier);
        QTest::newRow("ctrl-w-close")
            << int(Qt::Key_W) << Qt::KeyboardModifiers(Qt::ControlModifier);
        QTest::newRow("copy-extra-modifiers")
            << int(Qt::Key_C)
            << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
        QTest::newRow("copy-insert")
            << int(Qt::Key_Insert) << Qt::KeyboardModifiers(Qt::ControlModifier);
#ifdef Q_OS_MACOS
        QTest::newRow("native-cyrillic-copy")
            << 0x0421 << Qt::KeyboardModifiers(Qt::ControlModifier);
#endif
    }
    void originalKeys() {
        QFETCH(int, key);
        QFETCH(Qt::KeyboardModifiers, modifiers);
        const bool native = key == 0x0421;
        if (native && QApplication::platformName() != "cocoa")
            QSKIP("Requires native Cocoa keyboard-layout mapping");
        const bool copy = key == Qt::Key_C || key == Qt::Key_Insert || native;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "viewMode3Action")->trigger();
        const QStringList names{"alpha name.txt", QStringLiteral("бета 空.txt")};
        QList<QUrl> expected;
        if (copy) {
            for (const auto &name : names) {
                QFile file(fixture.filePath(name));
                QVERIFY(file.open(QIODevice::WriteOnly));
                expected.append(QUrl::fromLocalFile(file.fileName()));
            }
            window.openPath(fixture.path());
        } else
            openListedArchive(window, backend, fixture.filePath("клавиши 空.zip"),
                              {{names[0], 1, 1}, {names[1], 1, 1}});
        auto *list = table(window);
        QTRY_COMPARE(list->rowCount(), 2);
        list->setCurrentCell(0, 0);
        if (copy)
            list->selectAll();
        else
            list->clearSelection();
        QApplication::clipboard()->setText(QStringLiteral("clipboard sentinel"));
        QVERIFY(WindowActivation::waitForActive(window));
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        if (copy)
            QCOMPARE(list->selectionModel()->selectedRows().size(), 2);
        if (native) {
            QKeyEvent override(QEvent::ShortcutOverride, key, modifiers, 0, 8, 1u << 20);
            override.ignore();
            QApplication::sendEvent(list, &override);
            QVERIFY(override.isAccepted());
            QKeyEvent press(QEvent::KeyPress, key, modifiers, 0, 8, 1u << 20);
            QApplication::sendEvent(list, &press);
            QTest::keyRelease(list, Qt::Key_Control);
        } else
            QTest::keyClick(list, static_cast<Qt::Key>(key), modifiers);
        if (copy) {
            const auto actual = QApplication::clipboard()->mimeData()->urls();
            QCOMPARE(QSet<QUrl>(actual.begin(), actual.end()),
                     QSet<QUrl>(expected.begin(), expected.end()));
        } else if (key == Qt::Key_A)
            QCOMPARE(list->selectionModel()->selectedRows().size(), 2);
        else
            QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 5000);
        QTRY_VERIFY(!backend.busy());
        QCOMPARE(backend.extractCalls, 0);
        QCOMPARE(backend.testCalls, 0);
    }
    void clipboardConsumers_data() {
        QTest::addColumn<int>("pane");
        QTest::addColumn<int>("variant");
        for (int pane : {0, 1})
            for (int variant = 0; variant < 6; ++variant)
                QTest::newRow(
                    qPrintable(QStringLiteral("pane%1-scenario%2").arg(pane).arg(variant)))
                    << pane << variant;
    }
    void clipboardConsumers() {
        QFETCH(int, pane);
        QFETCH(int, variant);
        const bool archive = variant < 4;
        const bool alternative = variant % 2;
        const int mode = variant % 2 ? 3 : 0;
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QSettings settings(settingsPath, QSettings::IniFormat);
        settings.setValue("FM/AlternativeSelection", alternative);
        settings.sync();
        SaveRegLang(UString(variant % 3 ? "-" : "ru"), settingsPath);
        ReloadLang(settingsPath);
        QtNotifyLanguageChanged();
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QStringList names = {"alpha name.txt", QStringLiteral("бета 空.txt")};
        for (const auto &name : names) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected source bytes"), qint64(22));
        }
        QTemporaryDir peerFixture;
        QVERIFY(peerFixture.isValid());
        QFile peerFile(peerFixture.filePath("peer.txt"));
        QVERIFY(peerFile.open(QIODevice::WriteOnly));
        peerFile.close();
        SavePanelPath(pane, ToUString(fixture.path()), settingsPath);
        SavePanelPath(1 - pane, ToUString(peerFixture.path()), settingsPath);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *right = window.findChild<QWidget *>("filePanel1");
        if (!right || !right->isVisible())
            action(window, "twoPanelsAction")->trigger();
        auto *owner = window.findChild<QWidget *>(pane ? "filePanel1" : "filePanel0");
        auto *peer = window.findChild<QWidget *>(pane ? "filePanel0" : "filePanel1");
        QVERIFY(owner);
        QVERIFY(peer);
        auto *peerAddress = peer->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(peerAddress);
        QTRY_VERIFY(peerAddress->hasFocus());
        peerAddress->setText(peerFixture.path());
        QTest::keyClick(peerAddress, Qt::Key_Return);
        auto *peerList = peer->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(peerList->rowCount(), 1);
        QCOMPARE(peerList->model()->index(0, 0).data().toString(), QString("peer.txt"));
        auto *list = owner->findChild<FileTable *>("archiveTable");
        auto *address = owner->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        if (archive)
            openListedArchive(window, backend, fixture.filePath("выбор 空.zip"),
                              {{names[0], 1, 1}, {names[1], 1, 1}});
        QTRY_COMPARE(list->rowCount(), 2);
        const QString peerPath = peerAddress->text();
        action(window, qPrintable(QStringLiteral("viewMode%1Action").arg(mode)))->trigger();
        auto *view = mode == 3 ? static_cast<QWidget *>(list)
                               : static_cast<QWidget *>(owner->findChild<QListView *>("iconList"));
        QVERIFY(WindowActivation::waitForActive(window));
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        list->setCurrentCell(0, 0);
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        CRecordVector<UInt32> selected;
        auto *source = static_cast<FileTable *>(list)->sourcePanel;
        source->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), unsigned(2));
        QApplication::clipboard()->setText("clipboard sentinel");
        QTimer cancelExport;
        if (archive) {
            connect(&cancelExport, &QTimer::timeout, &backend, [&backend] {
                if (backend.busy())
                    backend.cancel();
                TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget());
            });
            cancelExport.start(5);
        }
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY(!backend.busy() && action(window, "refreshAction")->isEnabled());
        QList<QUrl> copied;
        if (archive) {
            // This fixture has no native archive reader to export entries.
            QCOMPARE(QApplication::clipboard()->text(), QString("clipboard sentinel"));
        } else {
            for (const auto &name : names)
                copied.append(QUrl::fromLocalFile(fixture.filePath(name)));
            const auto actual = QApplication::clipboard()->mimeData()->urls();
            QCOMPARE(QSet<QUrl>(actual.begin(), actual.end()),
                     QSet<QUrl>(copied.begin(), copied.end()));
        }
        source->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), unsigned(2));
        action(window, "deselectAllAction")->trigger();
        source->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), unsigned(0));
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        if (archive)
            QCOMPARE(QApplication::clipboard()->text(), QString("clipboard sentinel"));
        else {
            const auto actual = QApplication::clipboard()->mimeData()->urls();
            if (alternative) {
                const QString focused = list->currentIndex().data(Qt::UserRole).toString();
                QCOMPARE(actual, QList<QUrl>{QUrl::fromLocalFile(focused)});
            } else
                QCOMPARE(QSet<QUrl>(actual.begin(), actual.end()),
                         QSet<QUrl>(copied.begin(), copied.end()));
        }
        PanelKeys::focusAddress(address);
        address->selectAll();
        QTest::keyClick(address, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), address->text());
        QCOMPARE(peer->findChild<QLineEdit *>("archivePath")->text(), peerPath);
        QCOMPARE(peerList->rowCount(), 1);
        QCOMPARE(peerList->model()->index(0, 0).data().toString(), QString("peer.txt"));
        for (const auto &name : names) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("protected source bytes"));
        }
        QVERIFY(archive ? backend.extractCalls > 0 : backend.extractCalls == 0);
    }
    void bookmarkPriorityAndModifiedControlPageDown() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString saved = fixture.filePath(QStringLiteral("закладка 空"));
        const QString other = fixture.filePath("other folder");
        QVERIFY(QDir().mkpath(saved));
        QVERIFY(QDir().mkpath(other));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *list = table(window);
        address->setText(saved);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        QTest::keyClick(list, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        address->setText(other);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        list->setFocus();
        QTest::keyClick(list, Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
        QTRY_COMPARE(QFileInfo(address->text()).canonicalFilePath(), saved);
        action(window, "viewMode3Action")->trigger();
        openListedArchive(window, backend, fixture.filePath("nested source.zip"),
                          {{"inner.zip", 1, 1}});
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(WindowActivation::waitForActive(window));
        list->setFocus();
        list->setCurrentCell(0, 0);
        const int opens = backend.listCalls;
        QTest::keyClick(list, Qt::Key_PageDown, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(backend.listCalls, opens);
        QCOMPARE(backend.extractCalls, 0);
        QTest::keyClick(list, Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(window.findChild<QStackedWidget *>("fileViewStack")->currentIndex(), 1);
        QVERIFY(action(window, "viewMode0Action")->isChecked());
    }
    void rightControlNotificationHasBookmarkPriority_data() {
        QTest::addColumn<int>("digit");
        QTest::addColumn<bool>("nativeNotification");
        // Preserve the original Windows SendInput consumer regression.
        QTest::newRow("physical-1") << int(Qt::Key_1) << false;
        QTest::newRow("physical-8") << int(Qt::Key_8) << false;
        QTest::newRow("physical-9") << int(Qt::Key_9) << false;
        for (int digit = Qt::Key_0; digit <= Qt::Key_9; ++digit)
            QTest::newRow(qPrintable(QStringLiteral("native-%1").arg(digit - Qt::Key_0)))
                << digit << true;
    }
    void rightControlNotificationHasBookmarkPriority() {
        QFETCH(int, digit);
        QFETCH(bool, nativeNotification);
        if (nativeNotification && QApplication::platformName() != "windows" &&
            QApplication::platformName() != "cocoa" && QApplication::platformName() != "xcb")
            QSKIP("Native layout mapping requires the corresponding QPA plugin");
        const int slot = digit - Qt::Key_0;
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QDir root(QFileInfo(fixture.path()).canonicalFilePath());
        const QString saved = root.filePath(QStringLiteral("правая клавиша 空"));
        const QString other = root.filePath("other location");
        QVERIFY(QDir().mkpath(saved));
        QVERIFY(QDir().mkpath(other));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *address = window.findChild<QLineEdit *>("archivePath");
        auto *list = table(window);
        address->setText(saved);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        if (digit == Qt::Key_2) {
            QVERIFY(nativeNotification ? nativeDigitNotification(list, true, digit)
                                       : rightControlDigit(list, true, digit));
            QTRY_VERIFY(action(window, "twoPanelsAction")->isChecked());
            QTRY_VERIFY(action(window, "twoPanelsAction")->isEnabled());
            action(window, "twoPanelsAction")->trigger();
            QTRY_VERIFY(!action(window, "twoPanelsAction")->isChecked());
            list->setFocus();
            QTest::keyClick(list, Qt::Key_2, Qt::AltModifier | Qt::ShiftModifier);
        } else
            QVERIFY(nativeNotification ? nativeDigitNotification(list, true, digit)
                                       : rightControlDigit(list, true, digit));
        const QString settingsPath = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        const auto stored = [&] {
            return QSettings(settingsPath, QSettings::IniFormat)
                .value("FolderShortcuts")
                .toStringList();
        };
        QTRY_COMPARE(stored().size(), slot + 1);
        QCOMPARE(QDir::cleanPath(QDir::fromNativeSeparators(stored()[slot])), saved);
        address->setText(other);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        QVERIFY(nativeNotification ? nativeDigitNotification(list, false, digit)
                                   : rightControlDigit(list, false, digit));
        QTRY_COMPARE(QFileInfo(address->text()).canonicalFilePath(), saved);
        address->setText(other);
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(list->isEnabled());
        list->setFocus();
        QTest::keyClick(list, Qt::Key_1, Qt::ControlModifier);
        QCOMPARE(QFileInfo(address->text()).canonicalFilePath(), other);
        QVERIFY(action(window, "viewMode0Action")->isChecked());
        const auto bookmarks = stored();
        action(window, "viewMode3Action")->trigger();
        list->setFocus();
        QTest::keyClick(list, Qt::Key_1,
                        Qt::ControlModifier | Qt::ShiftModifier | Qt::KeypadModifier);
        QCOMPARE(stored(), bookmarks);
        QCOMPARE(QFileInfo(address->text()).canonicalFilePath(), other);
        QVERIFY(action(window, "viewMode3Action")->isChecked());
        QCOMPARE(backend.listCalls, 0);
    }
    void suppressedKeysRecoverWithoutCopyOrClose() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        const QString protectedPath = fixture.filePath("protected name.txt");
        QFile protectedFile(protectedPath);
        QVERIFY(protectedFile.open(QIODevice::WriteOnly));
        protectedFile.close();
        window.openPath(fixture.path());
        auto *list = table(window);
        QTRY_COMPARE(list->rowCount(), 1);
        QVERIFY(WindowActivation::waitForActive(window));
        list->setFocus();
        QTRY_VERIFY(list->hasFocus());
        list->setCurrentCell(0, 0);
        list->selectAll();
        auto *source = static_cast<FileTable *>(list)->sourcePanel;
        QApplication::clipboard()->setText("guard sentinel");
        {
            CPanel::CDisableNotify notify(*source);
            QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
            QTest::keyClick(list, Qt::Key_W, Qt::ControlModifier);
            QApplication::processEvents();
            QCOMPARE(QApplication::clipboard()->text(), QString("guard sentinel"));
            QVERIFY(window.isVisible());
            QVERIFY(!source->ShouldProcessNotifications());
        }
        QVERIFY(source->ShouldProcessNotifications());
        QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
        const QList<QUrl> copied{QUrl::fromLocalFile(protectedPath)};
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(), copied);
        QVERIFY(backend.prepareArchiveFixture(fixture.filePath("loading keys.zip")));
        backend.queueListAction([&] {
            const auto cancel = qScopeGuard([&] { backend.cancel(); });
            QTest::keyClick(list, Qt::Key_C, Qt::ControlModifier);
            QTest::keyClick(list, Qt::Key_W, Qt::ControlModifier);
            QCOMPARE(QApplication::clipboard()->mimeData()->urls(), copied);
            QVERIFY(window.isVisible());
        });
        window.openArchive(fixture.filePath("loading keys.zip"));
        QCOMPARE(backend.extractCalls, 0);
        QVERIFY(window.isVisible());
    }
    void modifiedSortAndDeleteCancellation() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        openListedArchive(window, backend, fixture.filePath("sort keys.zip"),
                          {{"alpha.txt", 1, 1}, {"beta.bin", 2, 2}});
        auto *list = table(window);
        QTRY_COMPARE(list->rowCount(), 2);
        QVERIFY(WindowActivation::waitForActive(window));
        list->setFocus();
        QTest::keyClick(list, Qt::Key_F6,
                        Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(list->model()->index(0, 0).data().toString(), QString("beta.bin"));
        QCOMPARE(backend.extractCalls, 0);

        const QString name = QStringLiteral("сохранить 空.txt");
        QFile file(fixture.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("unchanged"), qint64(9));
        file.close();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        list->setCurrentCell(filesystemRow(window, name), 0);
        list->selectRow(filesystemRow(window, name));
        list->setFocus();
        for (auto modifiers :
             {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::KeyboardModifiers(Qt::AltModifier),
              Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier)}) {
            int confirmations = 0;
            QTimer timer;
            dialogs_.connectResponder(timer, &window, [&] {
                for (QWidget *widget : QApplication::topLevelWidgets())
                    if (auto *box = dynamic_cast<QtDeleteConfirmation *>(widget);
                        box && dialogs_.ready(*box)) {
                        ++confirmations;
                        ReplyDelete(*box, false);
                    }
            });
            timer.start(1);
            QTest::keyClick(list, Qt::Key_Delete, modifiers);
            timer.stop();
#ifdef Q_OS_MACOS
            QCOMPARE(confirmations, 0);
#else
            QCOMPARE(confirmations, 1);
#endif
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("unchanged"));
            file.close();
        }
    }
};

QTEST_MAIN(PanelKeySourceTests)
#include "panel_key_source_tests.moc"
