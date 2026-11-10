#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/drag_event_position.h"
#include "support/overwrite_reply.h"
#include "support/progress_dialog.h"
#include "support/scoped_environment.h"
#include "support/window_activation.h"
#include "support/panel_keys.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include <QMessageBox>
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDrag>
#include <QDragLeaveEvent>
#include <QLabel>
#include <QLibrary>
#include <QMouseEvent>
#include <QProcess>
#include <QRegion>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QThread>
#include <atomic>
#include <memory>
#ifdef Q_OS_WIN
#include <chrono>
#include <thread>
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_LINUX
QRect WestonWindowFrame(qint64 pid) {
    QProcess scene;
    scene.start("weston-debug", {"scene-graph"});
    if (!scene.waitForFinished(3000) || scene.exitCode() != 0)
        return {};
    const QRegularExpression frame(
        QString(
            R"(role xdg_toplevel, PID %1, [^\n]*\n\s*position: \((-?\d+), (-?\d+)\) -> \((-?\d+), (-?\d+)\))")
            .arg(pid));
    const auto match = frame.match(QString::fromUtf8(scene.readAllStandardOutput()));
    if (!match.hasMatch())
        return {};
    const QPoint topLeft(match.captured(1).toInt(), match.captured(2).toInt());
    const QPoint bottomRight(match.captured(3).toInt(), match.captured(4).toInt());
    return QRect(topLeft, bottomRight - QPoint(1, 1));
}
#endif

int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).data().toString().normalized(
                QString::NormalizationForm_C) == name.normalized(QString::NormalizationForm_C))
            return row;
    return -1;
}
void Navigate(QWidget &panel, const QString &path) {
    auto *address = panel.findChild<QLineEdit *>("archivePath");
    address->setFocus();
    address->setText(path);
    QTest::keyClick(address, Qt::Key_Return);
}
bool CopyExport(const QMimeData *mime, const QString &destination, QString *error = nullptr) {
    if (!mime)
        return false;
    const auto urls = mime->urls();
    if (urls.size() != 1 || !urls.front().isLocalFile()) {
        if (error)
            *error = QStringLiteral("Expected one local file, got %1 URLs").arg(urls.size());
        return false;
    }
    QFile source(urls.front().toLocalFile());
    if (source.copy(destination))
        return true;
    if (error)
        *error =
            QStringLiteral("Copy from %1 failed: %2").arg(source.fileName(), source.errorString());
    return false;
}

#ifdef Q_OS_WIN
HWND ExplorerWindowForFolder(const QString &folder) {
    struct Search {
        QString name;
        HWND window = nullptr;
    } search{QFileInfo(folder).fileName()};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(parameter);
            wchar_t className[32]{};
            GetClassNameW(window, className, 32);
            if (!IsWindowVisible(window) || QString::fromWCharArray(className) != "CabinetWClass")
                return TRUE;
            wchar_t title[1024]{};
            GetWindowTextW(window, title, 1024);
            if (!QString::fromWCharArray(title).contains(search.name))
                return TRUE;
            search.window = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.window;
}
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
class NativeDropReceiver final : public QWidget {
  public:
    QString Destination;
    QString EnteredMarker;
    bool ReadPathsOnEnter = false;
    std::function<void()> BeforeDrop;
    std::atomic_bool Entered = false;
    bool DropReceived = false;
    QString CopyError;

    explicit NativeDropReceiver(const QString &destination) : Destination(destination) {
        setAcceptDrops(true);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls()) {
            if (ReadPathsOnEnter) {
                const auto urls = event->mimeData()->urls();
                if (urls.size() != 1 || !urls.front().isLocalFile() ||
                    !QFileInfo::exists(urls.front().toLocalFile()))
                    return;
            }
            Entered = true;
            if (!EnteredMarker.isEmpty())
                SourceExtractFixture::writeFile(EnteredMarker, "entered");
            event->acceptProposedAction();
        }
    }
    void dropEvent(QDropEvent *event) override {
        DropReceived = true;
        if (BeforeDrop)
            BeforeDrop();
        if (CopyExport(event->mimeData(), Destination, &CopyError)) {
            event->acceptProposedAction();
            if (!EnteredMarker.isEmpty())
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        }
    }
};
#endif
} // namespace

class DragDropUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    void archiveDragReachesNativeDropReceiver_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("external");
        QTest::addColumn<bool>("earlyRequest");
        QTest::newRow("drop") << false << false << false;
        QTest::newRow("escape") << true << false << false;
#ifdef Q_OS_LINUX
        QTest::newRow("external") << false << true << false;
        QTest::newRow("external-reads-on-enter") << false << true << true;
        QTest::newRow("peer-refresh") << false << false << false;
        if (qEnvironmentVariableIsSet("SEVENZIP_TEST_DOLPHIN")) {
            QTest::newRow("dolphin-cancel") << true << true << false;
            QTest::newRow("dolphin-clipboard") << false << true << false;
        }
#endif
    }
    void archiveDragReachesNativeDropReceiver() {
        QFETCH(bool, cancel);
        QFETCH(bool, external);
        QFETCH(bool, earlyRequest);
#ifdef Q_OS_WIN
        constexpr bool wayland = false;
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires an interactive Windows desktop");
        const auto press = [](bool down) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            return SendInput(1, &input, sizeof(input)) == 1;
        };
#else
        const bool wayland = QGuiApplication::platformName() == QStringLiteral("wayland");
        if (wayland &&
            (!external || earlyRequest || !qEnvironmentVariableIsSet("SEVENZIP_TEST_DOLPHIN")))
            QSKIP("Requires the isolated Dolphin session");
        if (!wayland && QGuiApplication::platformName() != QStringLiteral("xcb"))
            QSKIP("Requires an isolated X11 display");
        QLibrary x11("X11", 6), xtest("Xtst", 6);
        const auto open = reinterpret_cast<void *(*)(const char *)>(x11.resolve("XOpenDisplay"));
        const auto close = reinterpret_cast<int (*)(void *)>(x11.resolve("XCloseDisplay"));
        const auto flush = reinterpret_cast<int (*)(void *)>(x11.resolve("XFlush"));
        const auto motion = reinterpret_cast<int (*)(void *, int, int, int, unsigned long)>(
            xtest.resolve("XTestFakeMotionEvent"));
        const auto button = reinterpret_cast<int (*)(void *, unsigned, int, unsigned long)>(
            xtest.resolve("XTestFakeButtonEvent"));
        QVERIFY(open && close && flush && motion && button);
        auto *display = open(nullptr);
        QVERIFY(display);
        const auto closeDisplay = qScopeGuard([&] { close(display); });
        const auto pointAt = [&](const QPoint &point) {
            motion(display, -1, point.x(), point.y(), 0);
            flush(display);
        };
        const auto press = [&](bool down) {
            const bool sent = button(display, 1, down, 0);
            flush(display);
            return sent;
        };
#endif
        const auto release = qScopeGuard([&] { press(false); });

        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const ScopedTestEnvironment registry("SEVENZIP_OWNED_TEMP_REGISTRY",
                                             fixture.filePath("owned-temp"));
        const auto cleanup = qScopeGuard([] {
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                QDir(entry.path).removeRecursively();
        });
        const QString name = QStringLiteral("архивный файл 空.bin");
        const QByteArray payload("native drag\0payload", 19);
        const QString archive = fixture.filePath("input.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{ name, payload }}, "zip"));
        const auto original = SourceExtractFixture::readFile(archive);
        const auto outputFolder = wayland ? fixture.filePath("drop folder") : fixture.path();
        QVERIFY(QDir().mkpath(outputFolder));
        const auto destination = QDir(outputFolder).filePath(name);
        NativeDropReceiver receiver(destination);
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        receiver.setGeometry(screen.right() - 225, screen.top() + 100, 220, 300);
        const QString marker = fixture.filePath("receiver-entered");
        QProcess recipient;
        const auto stopRecipient = qScopeGuard([&] {
            if (recipient.state() != QProcess::NotRunning) {
                recipient.kill();
                recipient.waitForFinished();
            }
        });
        if (wayland) {
            recipient.setProcessChannelMode(QProcess::ForwardedChannels);
            recipient.start("dolphin",
                            {"-qwindowgeometry", "760x540", "--new-window", outputFolder});
            QVERIFY(recipient.waitForStarted());
        } else if (external) {
            recipient.start(QCoreApplication::applicationFilePath(),
                            {"--receive-native-file-drop", destination, marker,
                             earlyRequest ? "enter" : "drop"});
            QVERIFY(recipient.waitForStarted());
            QTRY_VERIFY(QFileInfo::exists(marker + ".ready"));
        } else {
            receiver.show();
            QVERIFY(QTest::qWaitForWindowExposed(&receiver));
        }
        ArchiveBackend backend;
        MainWindow window(&backend);
#ifdef Q_OS_LINUX
        if (wayland)
            window.setWindowFlag(Qt::FramelessWindowHint);
#endif
        QStringList warnings;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                warnings.append(message->text());
                message->reject();
            } else {
                TestProgressDialog::DismissVisibleResult(modal);
            }
        });
        dismiss.start(10);
        window.setGeometry(screen.left() + 10, screen.top() + 50, qMin(850, screen.width() - 260),
                           540);
        window.show();
        window.openArchive(archive);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
#ifdef Q_OS_LINUX
        if (!wayland)
#endif
            QVERIFY(WindowActivation::waitForActive(window));
        window.findChild<QAction *>("viewMode3Action")->trigger();
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QTRY_VERIFY(table->isVisible() && table->dragEnabled() && Row(*table, name) >= 0);
        FileTable *peer = nullptr;
        if (QByteArrayView(QTest::currentDataTag()) == "peer-refresh") {
            const QString folder = fixture.filePath("peer");
            QVERIFY(QDir().mkpath(folder));
            window.findChild<QAction *>("twoPanelsAction")->trigger();
            auto *peerPane = window.findChild<QWidget *>("filePanel1");
            QVERIFY(peerPane && peerPane->isVisible());
            Navigate(*peerPane, folder);
            peer = peerPane->findChild<FileTable *>("archiveTable");
            QVERIFY(peer);
            QTRY_VERIFY(peer->isEnabled());
            peer->sourcePanel->Set_AutoRefresh_Mode(false);
            for (int i = 0; i < 301; ++i)
                QVERIFY(SourceExtractFixture::writeFile(
                    QDir(folder).filePath(QString("peer-%1.txt").arg(i)), "peer update"));
            table->setFocus();
            QTRY_VERIFY(table->hasFocus());
            receiver.BeforeDrop = [peer] {
                QCOMPARE(peer->sourcePanel->RefreshListCtrl(), S_OK);
                QVERIFY(peer->rowCount() < 301);
            };
        }
        const auto sourceReady = [&] {
            return table->isEnabled() && table->dragEnabled() &&
                   table->currentIndex().data(QtPanelList::NameRole).toString() == name &&
                   table->selectionModel()->isSelected(table->currentIndex());
        };
        const QRect item = table->visualRect(table->model()->index(Row(*table, name), 0));
        const QPoint itemPoint(item.left() + 26, item.center().y());
        QPoint from = table->viewport()->mapToGlobal(itemPoint);
        QPoint to = external ? receiver.geometry().center()
                             : receiver.mapToGlobal(receiver.rect().center());
#ifdef Q_OS_LINUX
        if (wayland) {
            const QRect sourceFrame = WestonWindowFrame(QCoreApplication::applicationPid());
            QRect recipientFrame;
            QTRY_VERIFY((recipientFrame = WestonWindowFrame(recipient.processId())).isValid());
            QVERIFY(sourceFrame.isValid());
            from = sourceFrame.topLeft() + table->viewport()->mapTo(&window, itemPoint);
            const auto visible =
                QRegion(recipientFrame.adjusted(recipientFrame.width() / 3,
                                                recipientFrame.height() / 3, -35, -35))
                    .subtracted(QRegion(sourceFrame));
            QVERIFY(!visible.isEmpty());
            to = visible.begin()->center();
            pointAt(sourceFrame.bottomRight() - QPoint(16, 12));
            QVERIFY(press(true));
            QVERIFY(press(false));
            QTRY_VERIFY(window.isActiveWindow());
            if (QByteArrayView(QTest::currentDataTag()) == "dolphin-clipboard") {
                const auto keycode = reinterpret_cast<unsigned char (*)(void *, unsigned long)>(
                    x11.resolve("XKeysymToKeycode"));
                const auto key = reinterpret_cast<int (*)(void *, unsigned, int, unsigned long)>(
                    xtest.resolve("XTestFakeKeyEvent"));
                QVERIFY(keycode && key);
                const auto paste = [&] {
                    const auto control = keycode(display, 0xffe3); // Control_L
                    const auto v = keycode(display, 'v');
                    QVERIFY(control && v);
                    QVERIFY(key(display, control, true, 0));
                    QVERIFY(key(display, v, true, 0));
                    QVERIFY(key(display, v, false, 0));
                    QVERIFY(key(display, control, false, 0));
                    flush(display);
                };
                QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, itemPoint);
                QTRY_VERIFY(table->hasFocus());
                QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
                QTRY_VERIFY(QApplication::clipboard()->mimeData()->hasUrls());
                pointAt(to);
                QVERIFY(press(true));
                QVERIFY(press(false));
                QTRY_VERIFY(!window.isActiveWindow());
                paste();
                QTRY_COMPARE(SourceExtractFixture::readFile(destination), payload);
                QCOMPARE(SourceExtractFixture::readFile(archive), original);
                QApplication::clipboard()->clear();
                return;
            }
        }
#endif
        const QPoint threshold = from + QPoint(QApplication::startDragDistance() + 10, 0);
#ifdef Q_OS_WIN
        const auto nativePoint = [](const QPoint &point) {
            QCursor::setPos(point);
            POINT native{};
            GetCursorPos(&native);
            return native;
        };
        const POINT destinationPoint = nativePoint(to);
        const POINT thresholdPoint = nativePoint(threshold);
        nativePoint(from);
        QVERIFY(press(true));
        bool extractedBeforeRelease = false;
        std::jthread drive([&] {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(50ms);
            SetCursorPos(thresholdPoint.x, thresholdPoint.y);
            std::this_thread::sleep_for(250ms);
            SetCursorPos(destinationPoint.x, destinationPoint.y);
            for (int i = 0; i < 100 && !receiver.Entered; ++i)
                std::this_thread::sleep_for(20ms);
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                if (QFileInfo::exists(QDir(entry.path).filePath(name)))
                    extractedBeforeRelease = true;
            if (cancel) {
                INPUT escape[2]{};
                for (auto &event : escape) {
                    event.type = INPUT_KEYBOARD;
                    event.ki.wVk = VK_ESCAPE;
                }
                escape[1].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(2, escape, sizeof(INPUT));
                std::this_thread::sleep_for(100ms);
            }
            press(false);
        });
        if (cancel) {
            // The native driver must release the button even when OLE already
            // returned in response to Escape.
            QTRY_VERIFY_WITH_TIMEOUT(receiver.Entered, 5000);
            drive.join();
            QCoreApplication::processEvents();
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
            QVERIFY(!extractedBeforeRelease);
            QVERIFY(!QFileInfo::exists(destination));
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                QVERIFY(!QFileInfo::exists(QDir(entry.path).filePath(name)));
            QCOMPARE(SourceExtractFixture::readFile(archive), original);
            QTRY_VERIFY(sourceReady());
            return;
        }
#else
        bool checkedBeforeRelease = false;
        bool extractedBeforeRelease = false;
        int hoverTicks = 0;
        QTimer drive, watchdog;
        connect(&drive, &QTimer::timeout, &window, [&] {
            if (wayland    ? hoverTicks >= 30
                : external ? QFileInfo::exists(marker)
                           : receiver.Entered.load()) {
                drive.stop();
                checkedBeforeRelease = true;
                extractedBeforeRelease = QFileInfo::exists(destination);
                for (const auto &entry : QtOwnedTempRegistry().Entries())
                    if (QFileInfo::exists(QDir(entry.path).filePath(name)))
                        extractedBeforeRelease = true;
                if (cancel) {
                    if (wayland) {
                        pointAt(screen.bottomLeft() + QPoint(5, -5));
                        QTimer::singleShot(150, &window, [&] { press(false); });
                        return;
                    }
                    QDrag::cancel();
                }
                press(false);
            } else if (window.findChild<QDrag *>()) {
                pointAt(to);
                ++hoverTicks;
            }
        });
        connect(&watchdog, &QTimer::timeout, &window, [&] {
            drive.stop();
            press(false);
            QDrag::cancel();
        });
        watchdog.setSingleShot(true);
        pointAt(from);
        QVERIFY(press(true));
        QTimer::singleShot(50, &window, [&] { pointAt(threshold); });
        drive.start(10);
        watchdog.start(7000);
        if (cancel) {
            QTRY_VERIFY_WITH_TIMEOUT(checkedBeforeRelease, 8000);
            QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
            QVERIFY(!extractedBeforeRelease);
            QVERIFY(!QFileInfo::exists(destination));
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                QVERIFY(!QFileInfo::exists(QDir(entry.path).filePath(name)));
            QCOMPARE(SourceExtractFixture::readFile(archive), original);
            QTRY_VERIFY(sourceReady());
            return;
        }
#endif
        QTRY_VERIFY2_WITH_TIMEOUT(
            !warnings.isEmpty() || QFileInfo::exists(destination),
            qPrintable(QStringLiteral("Drag entered: %1; drop received: %2; %3")
                           .arg(receiver.Entered.load())
                           .arg(receiver.DropReceived)
                           .arg(receiver.CopyError)),
            8000);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
#ifdef Q_OS_WIN
        drive.join();
        QVERIFY(receiver.Entered);
#else
        QVERIFY(checkedBeforeRelease);
#endif
        QCOMPARE(extractedBeforeRelease, earlyRequest);
        QCOMPARE(SourceExtractFixture::readFile(destination), payload);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        if (peer)
            QTRY_VERIFY(peer->isEnabled() && peer->rowCount() >= 301);
        if (external && !wayland) {
            QTRY_COMPARE(recipient.state(), QProcess::NotRunning);
            QCOMPARE(recipient.exitCode(), 0);
        }
    }
#endif

    void archiveClipboardPastesBothSelectedEntriesIntoPeer_data() {
        QTest::addColumn<bool>("directory");
        QTest::newRow("two-files") << false;
        QTest::newRow("file-and-directory") << true;
    }

    void archiveClipboardPastesBothSelectedEntriesIntoPeer() {
        if (!qEnvironmentVariableIsSet("SEVENZIP_TEST_ARCHIVE_CLIPBOARD_PASTE"))
            QSKIP("Requires an isolated test process");
        QFETCH(bool, directory);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const ScopedTestEnvironment settings("SEVENZIP_SETTINGS_PATH",
                                             fixture.filePath("settings.ini"));
        const QString archive = fixture.filePath("source.zip");
        const QString output = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(output));
        const QString first = QStringLiteral("первый #.txt");
        const QString second =
            directory ? QStringLiteral("папка #") : QStringLiteral("second file.txt");
        const QString secondFile =
            directory ? second + QStringLiteral("/nested/данные.txt") : second;
        QVERIFY(NativeArchiveFixture::Write(archive, {{first, "first"}, {secondFile, "second"}},
                                            "zip"));
        const auto original = SourceExtractFixture::readFile(archive);
        const ScopedTestEnvironment registry("SEVENZIP_OWNED_TEMP_REGISTRY",
                                             fixture.filePath("owned-temp"));
        const auto cleanup = qScopeGuard([] {
            QApplication::clipboard()->clear();
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                QDir(entry.path).removeRecursively();
        });
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *twoPanels = window.findChild<QAction *>("twoPanelsAction");
        QVERIFY(twoPanels);
        QTRY_VERIFY(twoPanels->isEnabled());
        if (!twoPanels->isChecked())
            twoPanels->trigger();
        const auto enterPath = [](QWidget &pane, const QString &path) {
            auto *address = pane.findChild<QLineEdit *>("archivePath");
            QVERIFY(address);
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            address->setText(QDir::toNativeSeparators(path));
            QTest::keyClick(address, Qt::Key_Return);
        };
        auto *peer = window.findChild<QWidget *>("filePanel1");
        QVERIFY(peer);
        auto *destination = peer->findChild<FileTable *>("archiveTable");
        QVERIFY(destination);
        QTRY_VERIFY(destination->isVisible() && destination->isEnabled());
        enterPath(*peer, output);
        QTRY_COMPARE(window.focusWidget(), destination);
        QTRY_COMPARE(QDir::cleanPath(QDir::fromNativeSeparators(
                         ToQString(destination->sourcePanel->GetCurrentFolderPrefix()))),
                     output);
        auto *left = window.findChild<QWidget *>("filePanel0");
        QVERIFY(left);
        auto *source = left->findChild<FileTable *>("archiveTable");
        QVERIFY(source);
        QTRY_VERIFY(source->isEnabled());
        enterPath(*left, archive);
        QTRY_VERIFY(source->isEnabled() && Row(*source, first) >= 0 && Row(*source, second) >= 0);
        QTRY_COMPARE(window.focusWidget(), source);
        QTest::keySequence(source, QKeySequence::SelectAll);
        QTRY_COMPARE(source->selectionModel()->selectedRows().size(), 2);
        QTest::keySequence(window.focusWidget(), QKeySequence::Copy);
        QTRY_VERIFY(QApplication::clipboard()->mimeData());
        QTRY_COMPARE(QApplication::clipboard()->mimeData()->urls().size(), 2);
        QCOMPARE(window.focusWidget(), source);
        QTest::keyClick(window.focusWidget(), Qt::Key_Tab);
        QTRY_COMPARE(window.focusWidget(), destination);
        QTest::keySequence(window.focusWidget(), QKeySequence::Paste);
        QTRY_COMPARE(SourceExtractFixture::readFile(QDir(output).filePath(first)), "first");
        QTRY_COMPARE(SourceExtractFixture::readFile(QDir(output).filePath(secondFile)), "second");
        QTRY_VERIFY(Row(*destination, first) >= 0 && Row(*destination, second) >= 0);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }

    void archiveDragCopiesIntoTheHighlightedPeerFolder() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() != QStringLiteral("windows"))
            QSKIP("Requires the native Windows platform plugin");
#endif
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("source.zip");
        const QString name = QStringLiteral("данные #.txt");
        const QString folder = QStringLiteral("target folder");
        const QString destination = fixture.filePath(folder + '/' + name);
        QVERIFY(QDir().mkpath(fixture.filePath(folder)));
        QVERIFY(NativeArchiveFixture::Write(archive, {{name, "archive drag bytes"}}, "zip"));
        const QByteArray original = SourceExtractFixture::readFile(archive);
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(folder + "/seed.txt"), "seed"));
        QtWorkspaceApplication application(*qApp);
        MainWindow observer;
        observer.show();
        observer.openPath(fixture.filePath(folder));
        auto *observed = observer.findChild<FileTable *>("archiveTable");
        QVERIFY(observed);
        QTRY_VERIFY(observed->isEnabled() && Row(*observed, "seed.txt") >= 0);
        observed->sourcePanel->Set_AutoRefresh_Mode(false);
        ArchiveBackend backend;
        MainWindow window(&backend);
        QStringList errors;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &window, [&] {
            auto *modal = QApplication::activeModalWidget();
            if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                errors.append(message->text());
                message->accept();
            } else {
                TestProgressDialog::DismissVisibleResult(modal);
            }
        });
        dismiss.start(5);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *two = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(two->isEnabled());
        if (!two->isChecked())
            two->trigger();
        auto *left = window.findChild<QWidget *>("filePanel0");
        auto *right = window.findChild<QWidget *>("filePanel1");
        QVERIFY(left && right);
        auto *source = left->findChild<FileTable *>("archiveTable");
        auto *target = right->findChild<FileTable *>("archiveTable");
        QVERIFY(source && target);
        Navigate(*right, fixture.path());
        QTRY_VERIFY(target->isEnabled() && Row(*target, folder) >= 0);
        Navigate(*left, archive);
        QTRY_VERIFY(source->isEnabled() && Row(*source, name) >= 0);
        source->setCurrentCell(Row(*source, name), 0);
        source->selectRow(source->currentRow());
        QVERIFY(source->dragStarter && source->sourcePanel);
        bool cancel = true;
        bool accepted = false;
        QtPanelDragTransport transport(source, [&](QDrag &drag, Qt::DropActions actions) {
            const auto index = target->model()->index(Row(*target, folder), 0);
            const QPoint point = target->visualRect(index).center();
            QDragEnterEvent enter(DragEventPosition(point), actions, drag.mimeData(),
                                  Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(target->viewport(), &enter);
            if (!enter.isAccepted())
                return Qt::IgnoreAction;
            if (cancel) {
                QDragLeaveEvent leave;
                QCoreApplication::sendEvent(target->viewport(), &leave);
                return Qt::IgnoreAction;
            }
            QDropEvent drop(point, actions, drag.mimeData(), Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(target->viewport(), &drop);
            accepted = drop.isAccepted();
            return accepted ? drop.dropAction() : Qt::IgnoreAction;
        });
        source->sourcePanel->AttachDragServices(&transport);
        const auto detach = qScopeGuard([&] { source->sourcePanel->AttachDragServices(nullptr); });
        source->dragStarter();
        QVERIFY(!QFileInfo::exists(destination));
        QCOMPARE(Row(*observed, name), -1);
        cancel = false;
        source->dragStarter();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u'\n')));
        QVERIFY(accepted);
        QCOMPARE(SourceExtractFixture::readFile(destination), QByteArray("archive drag bytes"));
        QTRY_VERIFY(Row(*observed, name) >= 0);
        QVERIFY(!QFileInfo::exists(fixture.filePath(name)));
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }

    void archiveClipboardReportsFailedExtraction() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("повреждённый файл.bin");
        const QString archive = fixture.filePath("damaged.zip");
        const QByteArray payload("stored clipboard payload");
        QVERIFY(NativeArchiveFixture::Write(archive, {{name, payload}}, "zip", {}, true, true));
        QByteArray image = SourceExtractFixture::readFile(archive);
        const qsizetype offset = image.indexOf(payload);
        QVERIFY(offset >= 0);
        image[offset] = 'X';
        QVERIFY(SourceExtractFixture::writeFile(archive, image));
        const ScopedTestEnvironment registry("SEVENZIP_OWNED_TEMP_REGISTRY",
                                             fixture.filePath("owned-temp"));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(archive);
        QVERIFY(WindowActivation::waitForActive(window));
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled() && Row(*table, name) >= 0);
        table->setCurrentCell(Row(*table, name), 0);
        table->selectRow(table->currentRow());
        table->setFocus();
        QTRY_VERIFY(table->hasFocus());
        auto *clipboard = QApplication::clipboard();
        const auto clear = qScopeGuard([&] { clipboard->clear(); });
        clipboard->setText("previous clipboard contents");
        QStringList existingTemps, exportTemps;
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            existingTemps.append(entry.path);
        bool dismissed = false;
        QTimer closeResult;
        connect(&closeResult, &QTimer::timeout, &window, [&] {
            auto *result = TestProgressDialog::ResultWindow(QApplication::activeModalWidget());
            if (!result || !result->isVisible())
                return;
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                if (!existingTemps.contains(entry.path) && QFileInfo::exists(entry.path))
                    exportTemps.append(entry.path);
            dismissed = true;
            TestProgressDialog::CloseResult(*result);
        });
        closeResult.start(5);
        QTest::keySequence(table, QKeySequence::Copy);
        QVERIFY(dismissed);
        QCOMPARE(clipboard->text(), QString("previous clipboard contents"));
        QCOMPARE(
            window.statusBar()->currentMessage(),
            QCoreApplication::translate(
                "QtFileClipboard",
                "Complete extraction could not be confirmed. The clipboard was kept unchanged."));
        QVERIFY(!exportTemps.isEmpty());
        for (const auto &path : exportTemps)
            QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
    }

    void fileExportCanBeCopiedByRecipient_data() {
        QTest::addColumn<bool>("archiveSource");
        QTest::addColumn<bool>("drag");
        QTest::addColumn<bool>("replaceClipboard");
        QTest::addColumn<bool>("cancelExport");
        QTest::newRow("archive-drag") << true << true << false << false;
        QTest::newRow("archive-clipboard") << true << false << false << false;
        QTest::newRow("filesystem-clipboard") << false << false << false << false;
        QTest::newRow("archive-clipboard-replaced") << true << false << true << false;
        QTest::newRow("archive-clipboard-cancel-and-retry") << true << false << false << true;
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QStringLiteral("windows"))
            QTest::newRow("explorer-clipboard") << true << false << false << false;
#endif
    }
    void fileExportCanBeCopiedByRecipient() {
        QFETCH(bool, archiveSource);
        QFETCH(bool, drag);
        QFETCH(bool, replaceClipboard);
        QFETCH(bool, cancelExport);
        QTemporaryDir input, output;
        QVERIFY(input.isValid() && output.isValid());
        const auto registryVariable = "SEVENZIP_OWNED_TEMP_REGISTRY";
        const QByteArray previousRegistry = qgetenv(registryVariable);
        const QString registryPath = input.filePath("owned-temp");
        const auto cleanup = qScopeGuard([&] {
            for (const auto &entry : QtOwnedTempRegistry(registryPath).Entries())
                QDir(entry.path).removeRecursively();
            if (previousRegistry.isNull())
                qunsetenv(registryVariable);
            else
                qputenv(registryVariable, previousRegistry);
        });
        QVERIFY(qputenv(registryVariable, registryPath.toLocal8Bit()));
        QCOMPARE(qEnvironmentVariable(registryVariable), registryPath);
        const QString name = QStringLiteral("файл # %23 с пробелами 空.bin");
        const QByteArray bytes("export\0bytes", 12);
        const QString source = input.filePath(name);
        QVERIFY(SourceExtractFixture::writeFile(source, bytes));
        const QString password = QStringLiteral("clipboard fixture password");
        const bool encrypted = replaceClipboard || cancelExport;
        const QString archive = input.filePath(encrypted ? "source.7z" : "source.zip");
        if (encrypted)
            QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, "7z", password, false));
        else if (archiveSource)
            QVERIFY(NativeArchiveFixture::WriteFiles(archive, {source}, "zip"));
        const QByteArray original = SourceExtractFixture::readFile(archive);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(archiveSource ? archive : input.path());
        QVERIFY(WindowActivation::waitForActive(window));
        window.findChild<QAction *>("viewMode3Action")->trigger();
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QTRY_VERIFY(table->isEnabled() && Row(*table, name) >= 0);
        const auto item = table->model()->index(Row(*table, name), 0);
        table->scrollTo(item);
        const QRect rect = table->visualRect(item);
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(rect.left() + 26, rect.center().y()));
        QTRY_VERIFY(table->hasFocus());
        QTRY_VERIFY(PanelIndexSelected(item));

        auto *clipboard = QApplication::clipboard();
        const auto clearClipboard = qScopeGuard([&] { clipboard->clear(); });
        const QString destination = output.filePath(name);
        QString exportSummary;
        QTimer closeResult;
        connect(&closeResult, &QTimer::timeout, &window, [&] {
            auto *result = TestProgressDialog::ResultWindow(QApplication::activeModalWidget());
            if (!result)
                return;
            exportSummary = result->findChild<QLabel *>("IDT_PROGRESS_STATUS")->text();
            TestProgressDialog::CloseResult(*result);
        });
        closeResult.start(5);
        if (drag) {
            QtPanelDragTransport transport(table, [&](QDrag &operation, Qt::DropActions actions) {
                if (!(actions & Qt::CopyAction) || !CopyExport(operation.mimeData(), destination))
                    return Qt::IgnoreAction;
                static_cast<const QtPanelDragMime *>(operation.mimeData())
                    ->Data()
                    .FinishExternalReceiver();
                return Qt::CopyAction;
            });
            QVERIFY(table->dragStarter && table->sourcePanel);
            table->sourcePanel->AttachDragServices(&transport);
            const auto detach =
                qScopeGuard([&] { table->sourcePanel->AttachDragServices(nullptr); });
            table->dragStarter();
        } else {
            clipboard->setText("clipboard sentinel");
            auto *copy = window.findChild<QAction *>("copyFilesAction");
            QVERIFY(copy);
            QTRY_VERIFY(copy->isEnabled());
            const QString newer = QStringLiteral("newer clipboard text");
            QStringList existingTemps, exportTemps;
            QTimer replacement;
            if (cancelExport) {
                const QString blockedRegistry = input.filePath("blocked registry");
                QVERIFY(SourceExtractFixture::writeFile(blockedRegistry, "not a directory"));
                {
                    const auto restoreRegistry =
                        qScopeGuard([&] { qputenv(registryVariable, registryPath.toLocal8Bit()); });
                    QVERIFY(qputenv(registryVariable, blockedRegistry.toLocal8Bit()));
                    QCOMPARE(qEnvironmentVariable(registryVariable), blockedRegistry);
                    QTest::keySequence(table, QKeySequence::Copy);
                    QCOMPARE(clipboard->text(), QString("clipboard sentinel"));
                    QCOMPARE(window.statusBar()->currentMessage(),
                             QCoreApplication::translate("SevenZipDiagnostics",
                                                         "Can't create temp folder"));
                    QVERIFY(table->hasFocus());
                }
                int prompts = 0;
                QTimer answer;
                connect(&answer, &QTimer::timeout, &window, [&] {
                    auto *prompt =
                        dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
                    if (!prompt)
                        return;
                    if (++prompts == 1)
                        prompt->reject();
                    else {
                        answer.stop();
                        prompt->findChild<QLineEdit *>("passwordEdit")->setText(password);
                        prompt->accept();
                    }
                });
                answer.start(5);
                QTest::keySequence(table, QKeySequence::Copy);
                QCOMPARE(prompts, 1);
                QCOMPARE(exportSummary, QCoreApplication::translate(
                                            "QtTaskResultDialog", "The operation was cancelled."));
                QCOMPARE(clipboard->text(), QString("clipboard sentinel"));
                QCOMPARE(window.statusBar()->currentMessage(),
                         QCoreApplication::translate("mainwindow_archivecommands",
                                                     "Operation canceled."));
                QVERIFY(WindowActivation::waitForActive(window));
                QTRY_VERIFY(table->hasFocus());
                QTRY_VERIFY(PanelIndexSelected(item));
                QTest::keySequence(table, QKeySequence::Copy);
                QCOMPARE(prompts, 2);
                QVERIFY(window.statusBar()->currentMessage().isEmpty());
            }
            if (replaceClipboard) {
                for (const auto &entry : QtOwnedTempRegistry().Entries())
                    existingTemps.append(entry.path);
                connect(&replacement, &QTimer::timeout, &window, [&] {
                    auto *prompt =
                        dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
                    if (!prompt)
                        return;
                    replacement.stop();
                    for (const auto &entry : QtOwnedTempRegistry().Entries())
                        if (!existingTemps.contains(entry.path))
                            exportTemps.append(entry.path);
                    clipboard->setText(newer);
                    prompt->findChild<QLineEdit *>("passwordEdit")->setText(password);
                    prompt->accept();
                });
                replacement.start(5);
            }
            if (!cancelExport)
                QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
            if (replaceClipboard) {
                QVERIFY(!exportTemps.isEmpty());
                QCOMPARE(clipboard->text(), newer);
                for (const auto &path : exportTemps)
                    QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
                QCOMPARE(SourceExtractFixture::readFile(archive), original);
                return;
            }
            QTRY_VERIFY(clipboard->mimeData() && clipboard->mimeData()->hasUrls());
#ifdef Q_OS_WIN
            if (QByteArrayView(QTest::currentDataTag()) == "explorer-clipboard") {
                QVERIFY(QProcess::startDetached("explorer.exe",
                                                {QDir::toNativeSeparators(output.path())}));
                HWND explorer = nullptr;
                QTRY_VERIFY((explorer = ExplorerWindowForFolder(output.path())));
                const auto closeExplorer = qScopeGuard([&] {
                    if (ExplorerWindowForFolder(output.path()) == explorer)
                        PostMessageW(explorer, WM_CLOSE, 0, 0);
                });
                SetForegroundWindow(explorer);
                QTRY_COMPARE(GetForegroundWindow(), explorer);
                INPUT paste[4]{};
                for (auto &event : paste)
                    event.type = INPUT_KEYBOARD;
                paste[0].ki.wVk = paste[3].ki.wVk = VK_CONTROL;
                paste[1].ki.wVk = paste[2].ki.wVk = 'V';
                paste[2].ki.dwFlags = paste[3].ki.dwFlags = KEYEVENTF_KEYUP;
                QCOMPARE(SendInput(4, paste, sizeof(INPUT)), UINT(4));
                QTRY_COMPARE(SourceExtractFixture::readFile(destination), bytes);
                QCOMPARE(SourceExtractFixture::readFile(archive), original);
                return;
            }
#endif
            if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
                QVERIFY2(CopyExport(clipboard->mimeData(), destination),
                         qPrintable(clipboard->text()));
            } else {
                QProcess recipient;
                recipient.start(QCoreApplication::applicationFilePath(),
                                {"--copy-from-system-clipboard", destination});
                QVERIFY2(recipient.waitForStarted(), qPrintable(recipient.errorString()));
                QTRY_COMPARE_WITH_TIMEOUT(recipient.state(), QProcess::NotRunning, 10000);
                QCOMPARE(recipient.exitStatus(), QProcess::NormalExit);
                QCOMPARE(recipient.exitCode(), 0);
            }
        }
        QCOMPARE(SourceExtractFixture::readFile(output.filePath(name)), bytes);
        QCOMPARE(SourceExtractFixture::readFile(source), bytes);
        if (archiveSource)
            QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }

    void viewModesPreserveDragInteraction_data() {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("enabled") << true;
        QTest::newRow("disabled") << false;
    }
    void viewModesPreserveDragInteraction() {
        QFETCH(bool, enabled);
        const std::unique_ptr<QStyle> style(QStyleFactory::create(QStringLiteral("Fusion")));
        QVERIFY(style);
        QStandardItemModel model;
        model.appendRow(new QStandardItem(QStringLiteral("drag item")));
        FileIconList view;
        view.setModel(&model);
        view.setDragEnabled(enabled);
        int exports = 0;
        view.dragStarter = [&] { ++exports; };
        view.ConfigureLargeIcons();
        view.resize(640, 320);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto drag = [&] {
            const auto index = model.index(0, 0);
            view.setCurrentIndex(index);
            const QPoint start = view.visualRect(index).center();
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
            const QPoint finish = start + QPoint(QApplication::startDragDistance() + 2, 0);
            QMouseEvent move(QEvent::MouseMove, finish, view.viewport()->mapToGlobal(finish),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view.viewport(), &move);
            QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, finish);
        };
        drag();
        QCOMPARE(exports, int(enabled));
        QFont font = view.font();
        font.setPointSizeF(qMax(9.0, font.pointSizeF()) + 1);
        view.setFont(font);
        QCoreApplication::processEvents();
        drag();
        QCOMPARE(exports, enabled ? 2 : 0);
        view.setStyle(style.get());
        QCoreApplication::processEvents();
        drag();
        QCOMPARE(exports, enabled ? 3 : 0);
    }

    void externalMiddleButtonUsesDropMenu() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QFile file(root.filePath(QStringLiteral("middle protected δ.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("middle button control"), qint64(21));
        file.close();
        SavePanelPath(0, ToUString(root.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        auto *panel = window.findChild<QWidget *>("filePanel0");
        QVERIFY(panel);
        Navigate(*panel, root.path());
        QTRY_VERIFY(table->isEnabled() && Row(*table, QFileInfo(file).fileName()) >= 0);
        table->setFocus();
        QTRY_COMPARE(window.focusWidget(), table);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::MiddleButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool menuShown = false;
        QTimer cancel;
        connect(&cancel, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(widget);
                if (menu && menu->objectName() == "dragContextMenu" && menu->isVisible()) {
                    menuShown = true;
                    QVERIFY(UiInspection::writeReport(*menu, "drag-middle-menu"));
                    QTest::keyClick(menu, Qt::Key_Escape);
                }
            }
        });
        cancel.start(1);
        // Buttons are released for Drop. DragEnter must retain the menu decision.
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &drop);
        cancel.stop();
        QVERIFY(menuShown);
        QVERIFY(!drop.isAccepted());
        QTRY_COMPARE(window.focusWidget(), table);
        QVERIFY(!backend.busy());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("middle button control"));
    }

    void archiveSourceCancelsThroughLiveController_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("right");
        QTest::newRow("table") << false << false;
        QTest::newRow("icons") << true << false;
        QTest::newRow("right-gesture") << false << true;
    }
    void archiveSourceCancelsThroughLiveController() {
        QFETCH(bool, icons);
        QFETCH(bool, right);
        QTemporaryDir root;
        const QString name = QStringLiteral("исходный с пробелом.txt");
        QFile input(root.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("controller bytes"), qint64(16));
        input.close();
        const QString hoverName = QStringLiteral("hover after cancel.txt");
        QFile hoverFile(root.filePath(hoverName));
        QVERIFY(hoverFile.open(QIODevice::WriteOnly));
        QCOMPARE(hoverFile.write("hover bytes"), qint64(11));
        hoverFile.close();
        const QString archive = root.filePath("source.zip");
        ArchiveBackend backend;
        QSignalSpy finished(&backend, &ArchiveBackend::finished);
        QVERIFY(
            NativeArchiveFixture::WriteFiles(archive, {input.fileName(), hoverFile.fileName()}));
        QFile original(archive);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = original.readAll();
        original.close();
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *table = dynamic_cast<FileTable *>(window.findChild<FileTable *>("archiveTable"));
        QVERIFY(table && table->dragStarter);
        QTRY_VERIFY(table->isEnabled() && Row(*table, name) >= 0);
        if (icons)
            window.findChild<QAction *>("viewMode0Action")->trigger();
        table->selectRow(Row(*table, name));
        table->setCurrentCell(Row(*table, name), 0);
        CMyComPtr<IFolderFolder> provider = table->sourcePanel->GetFolder();
        finished.clear();
        QSet<QString> previousRoots;
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            previousRoots.insert(entry.path);
        QStringList dragRoots;
        bool cancelledNativeDrag = false;
        QTimer cancelNativeDrag;
        const bool xcb = QGuiApplication::platformName() == QStringLiteral("xcb");
        const int outerLoop = QThread::currentThread()->loopLevel();
        connect(&cancelNativeDrag, &QTimer::timeout, &window, [&] {
            // The offscreen plugin returns IgnoreAction itself. A real XCB
            // drag waits for a drop/cancel event; cancel only its entered loop,
            // after archive extraction and publication of the owned QDrag.
            if (window.findChild<QDrag *>() && QThread::currentThread()->loopLevel() > outerLoop) {
                cancelledNativeDrag = true;
                cancelNativeDrag.stop();
                QDrag::cancel();
            }
        });
        if (xcb)
            cancelNativeDrag.start(5);
        if (right) {
            QWidget *viewport = table->viewport();
            const QPoint point =
                table->visualRect(table->model()->index(Row(*table, name), 0)).center();
            const QPoint global = viewport->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::RightButton,
                              Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &press);
            QContextMenuEvent context(QContextMenuEvent::Mouse, point, global);
            QCoreApplication::sendEvent(viewport, &context);
            QVERIFY(!QApplication::activePopupWidget());
            const QPoint moved = point + QPoint(QApplication::startDragDistance() + 1, 0);
            QMouseEvent move(QEvent::MouseMove, moved, viewport->mapToGlobal(moved), Qt::NoButton,
                             Qt::RightButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, moved, viewport->mapToGlobal(moved),
                                Qt::RightButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &release);
            // A refused right drag must not suppress the next ordinary click.
            // Exercise platforms delivering their context notification on press.
            bool normalMenu = false;
            unsigned menus = 0;
            QTimer closeMenu;
            connect(&closeMenu, &QTimer::timeout, &window, [&] {
                if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    ++menus;
                    normalMenu = menu->objectName() == "panelContextMenu";
                    menu->close();
                }
            });
            closeMenu.start(5);
            QCoreApplication::sendEvent(viewport, &context);
            QCOMPARE(menus, 0u); // A delayed notification still belongs to the consumed drag.
            QCoreApplication::sendEvent(viewport, &press);
            QCoreApplication::sendEvent(viewport, &context);
            QCOMPARE(menus, 0u);
            QMouseEvent clicked(QEvent::MouseButtonRelease, point, global, Qt::RightButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport, &clicked);
            closeMenu.stop();
            QCOMPARE(menus, 1u);
            QVERIFY(normalMenu);
        } else if (icons) {
            auto *list = dynamic_cast<FileIconList *>(window.findChild<QListView *>("iconList"));
            QVERIFY(list && list->isVisible() && list->dragEnabled() && list->dragStarter);
            list->dragStarter();
        } else
            table->dragStarter();
        cancelNativeDrag.stop();
        if (xcb && !right)
            QVERIFY(cancelledNativeDrag);
        for (const auto &entry : QtOwnedTempRegistry().Entries())
            if (!previousRoots.contains(entry.path) &&
                QFileInfo::exists(QDir(entry.path).filePath(name)))
                dragRoots.append(entry.path);
        const bool nativeOle = QGuiApplication::platformName() == QStringLiteral("windows");
        if (right || nativeOle)
            QVERIFY(dragRoots.isEmpty());
#ifdef Q_OS_MACOS
        else
            QVERIFY(!dragRoots.isEmpty());
#endif
        QVERIFY(window.isVisible());
        QVERIFY(!backend.busy());
        QCOMPARE(table->sourcePanel->GetFolder(), provider.Interface());
        QTRY_VERIFY(Row(*table, name) >= 0 &&
                    PanelIndexSelected(table->model()->index(Row(*table, name), 0)));
        for (const QString &path : dragRoots) {
#ifdef Q_OS_MACOS
            // Default Cocoa IgnoreAction does not confirm that Finder stopped
            // reading. The live controller must keep these exposed bytes safe.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QFile delivered(QDir(path).filePath(name));
            QVERIFY(delivered.open(QIODevice::ReadOnly));
            QCOMPARE(delivered.readAll(), QByteArray("controller bytes"));
            delivered.close();
            QtOwnedTempRegistry registry;
            bool found = false;
            for (const auto &entry : registry.Entries()) {
                if (entry.path != path)
                    continue;
                found = true;
                QVERIFY(entry.externallyProtected && !entry.active);
                QVERIFY(!registry.ClaimForDeletion(entry));
            }
            QVERIFY(found);
            QVERIFY(QDir(path).removeRecursively()); // Only this fixture's extracted root.
#else
            QVERIFY(!QFileInfo::exists(path));
#endif
        }
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes);
        if (!right) {
            QAbstractItemView *view = icons ? window.findChild<QListView *>("iconList")
                                            : static_cast<QAbstractItemView *>(table);
            QVERIFY(view && view->isVisible());
            const int hoverRow = Row(*table, hoverName);
            QVERIFY(hoverRow >= 0);
            const QModelIndex hovered = view->model()->index(hoverRow, 0);
            const QRect cell = view->visualRect(hovered);
            const QPoint namePoint(cell.left() + 26, cell.center().y());
            QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                              icons ? cell.center() : namePoint);
            QTRY_COMPARE(view->currentIndex().siblingAtColumn(0).data().toString(), hoverName);
        }
    }

    void externalDragKeepsSelectionAndDoesNotOpenFolder_data() {
        QTest::addColumn<int>("rejection");
        QTest::newRow("accepted") << 0;
        QTest::newRow("rejected-entry-then-accepted") << 1;
        QTest::newRow("rejected-drop-then-accepted") << 2;
    }
    void externalDragKeepsSelectionAndDoesNotOpenFolder() {
        QFETCH(int, rejection);
        QTemporaryDir root;
        const QString folder = QStringLiteral("назначение");
        QVERIFY(QDir().mkpath(root.filePath(folder)));
        QFile file(root.filePath("selected.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        SavePanelPath(0, ToUString(root.path()), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        Navigate(*window.findChild<QWidget *>("filePanel0"), root.path());
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled() && Row(*table, folder) >= 0 &&
                    Row(*table, "selected.txt") >= 0);
        const int selected = Row(*table, "selected.txt");
        table->selectRow(selected);
        table->setCurrentCell(selected, 0);
        const QModelIndex destination = table->model()->index(Row(*table, folder), 0);
        const QRect cell = table->visualRect(destination);
        const QPoint point = cell.center();
        const QPoint namePoint(cell.left() + 26, point.y());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(file.fileName())});
        if (rejection == 1) {
            QDragEnterEvent rejected(DragEventPosition(point), Qt::LinkAction, &mime,
                                     Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(table->viewport(), &rejected);
            QVERIFY(!rejected.isAccepted());
            QVERIFY(!destination.data(Qt::UserRole + 19).toBool());
            // A rejected entry has no matching DragLeave. Selection and the
            // next supported drag still work.
            QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, namePoint);
            QTRY_COMPARE(table->currentRow(), destination.row());
            table->selectRow(selected);
            table->setCurrentCell(selected, 0);
        }
        QDragEnterEvent enter(DragEventPosition(point), Qt::CopyAction, &mime, Qt::LeftButton,
                              Qt::NoModifier);
        QCoreApplication::sendEvent(table->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QVERIFY(destination.data(Qt::UserRole + 19).toBool());
        if (rejection == 2) {
            QDropEvent rejected(point, Qt::LinkAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(table->viewport(), &rejected);
            QVERIFY(!rejected.isAccepted());
            QVERIFY(!destination.data(Qt::UserRole + 19).toBool());
            QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, namePoint);
            QTRY_COMPARE(table->currentRow(), destination.row());
            QVERIFY(QDir(root.filePath(folder)).isEmpty());
            table->selectRow(selected);
            table->setCurrentCell(selected, 0);
            QDragEnterEvent retry(DragEventPosition(point), Qt::CopyAction, &mime, Qt::LeftButton,
                                  Qt::NoModifier);
            QCoreApplication::sendEvent(table->viewport(), &retry);
            QVERIFY(retry.isAccepted());
            QVERIFY(destination.data(Qt::UserRole + 19).toBool());
        }
        QTest::mouseMove(table->viewport(), point);
        QTest::qWait(450);
        QCOMPARE(table->currentRow(), selected);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(table->viewport(), &leave);
        QVERIFY(!destination.data(Qt::UserRole + 19).toBool());
        QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, namePoint);
        QTRY_COMPARE(table->currentRow(), destination.row());
        QCOMPARE(
            QFileInfo(window.findChild<QLineEdit *>("archivePath")->text()).canonicalFilePath(),
            QFileInfo(root.path()).canonicalFilePath());
    }

    void externalDropCanRetryAnOverwrite_data() {
        QTest::addColumn<bool>("directory");
        QTest::addColumn<bool>("unreadable");
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("missing");
        QTest::newRow("file") << false << false << false << false;
        QTest::newRow("directory-url") << true << false << false << false;
        QTest::newRow("batch-copy-with-missing-source") << false << false << false << true;
        QTest::newRow("batch-move-with-missing-source") << false << false << true << true;
#ifdef Q_OS_UNIX
        QTest::newRow("batch-with-unreadable-source") << false << true << false << false;
        QTest::newRow("merge-with-unreadable-source") << true << true << true << false;
#endif
    }
    void externalDropCanRetryAnOverwrite() {
        QFETCH(bool, directory);
        QFETCH(bool, unreadable);
        QFETCH(bool, move);
        QFETCH(bool, missing);
        QTemporaryDir source, target;
        QVERIFY(source.isValid() && target.isValid());
        const QString entry = directory ? QString("same folder") : QString("same name.txt");
        const QString inputEntry = source.filePath(entry);
        const QString outputEntry = target.filePath(entry);
        if (directory)
            QVERIFY(QDir().mkpath(inputEntry) && QDir().mkpath(outputEntry));
        const QString input = directory ? inputEntry + "/item.txt" : inputEntry;
        const QString output = directory ? outputEntry + "/item.txt" : outputEntry;
        QVERIFY(SourceExtractFixture::writeFile(input, "incoming"));
        QVERIFY(SourceExtractFixture::writeFile(output, "original"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(target.path());
        auto *view = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(view->isEnabled() && Row(*view, entry) >= 0);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(directory ? inputEntry + '/' : inputEntry)});
        const QString blocked = directory && move ? inputEntry : source.filePath("second.txt");
        QFile::Permissions permissions;
        const auto restore = qScopeGuard([&] {
            if (unreadable && QFileInfo::exists(blocked))
                QFile::setPermissions(blocked, permissions);
        });
        if (unreadable) {
            if (!move)
                QVERIFY(SourceExtractFixture::writeFile(blocked, "other bytes"));
            permissions = QFile::permissions(blocked);
            QVERIFY(QFile::setPermissions(blocked, move ? QFile::WriteOwner | QFile::ExeOwner
                                                        : QFile::WriteOwner));
            if (QFileInfo(blocked).isReadable())
                QSKIP("The current user can still read the file");
        }
        if (missing || (unreadable && !move))
            mime.setUrls({QUrl::fromLocalFile(input), QUrl::fromLocalFile(blocked)});
        int prompts = 0;
        QString report;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &window, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                menu && menu->isVisible() && menu->objectName() == "dragContextMenu") {
                for (auto *action : menu->actions())
                    if (action->data().toUInt() ==
                        (NDragMenu::k_Copy_Base |
                         (move ? NDragMenu::k_MenuFlag_Move : NDragMenu::k_MenuFlag_Copy))) {
                        menu->setActiveAction(action);
                        QTest::keyClick(menu, Qt::Key_Return);
                        return;
                    }
                menu->close();
            }
            if (auto *dialog =
                    dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget())) {
                ++prompts;
                ReplyOverwrite(*dialog, prompts == 1 ? IDCANCEL : IDYES);
            } else if (auto *result =
                           TestProgressDialog::ResultWindow(QApplication::activeModalWidget())) {
                report = TestProgressDialog::ReportText(*result);
                TestProgressDialog::CloseResult(*result);
            } else if (auto *error =
                           qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                report = error->text();
                error->accept();
            }
        });
        answer.start(5);
        const QPoint point(5, view->viewport()->height() - 5);
        const auto actions =
            move ? Qt::CopyAction | Qt::MoveAction : Qt::DropActions(Qt::CopyAction);
        for (int attempt = unreadable || missing ? 0 : 1; attempt <= 2; ++attempt) {
            QDragEnterEvent enter(DragEventPosition(point), actions, &mime, Qt::MiddleButton,
                                  Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &enter);
            QVERIFY(enter.isAccepted());
            QDropEvent drop(point, actions, &mime, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &drop);
            QCOMPARE(prompts, attempt);
            QTRY_VERIFY(view->isEnabled());
            QCOMPARE(SourceExtractFixture::readFile(input),
                     move && attempt == 2 ? QByteArray() : QByteArray("incoming"));
            QCOMPARE(SourceExtractFixture::readFile(output),
                     attempt < 2 ? QByteArray("original") : QByteArray("incoming"));
            if (attempt == 0) {
                QVERIFY2(QDir::fromNativeSeparators(report).contains(blocked), qPrintable(report));
                if (!directory)
                    QVERIFY(!QFileInfo::exists(target.filePath("second.txt")));
                if (missing)
                    QVERIFY(SourceExtractFixture::writeFile(blocked, "other bytes"));
                else
                    QVERIFY(QFile::setPermissions(blocked, permissions));
            }
        }
        if (missing || (unreadable && !move))
            QCOMPARE(SourceExtractFixture::readFile(target.filePath("second.txt")),
                     QByteArray("other bytes"));
    }

    void internalDropCanRetryAfterDestinationAccessReturns() {
#ifndef Q_OS_UNIX
        QSKIP("Requires POSIX directory search permissions");
#endif
        QTemporaryDir source, target;
        QVERIFY(source.isValid() && target.isValid());
        const QString name = "incoming.txt";
        const QString destination = target.filePath("folder");
        QVERIFY(QDir().mkdir(destination));
        QVERIFY(QDir().mkdir(source.filePath("folder")));
        QVERIFY(SourceExtractFixture::writeFile(source.filePath(name), "payload"));
        const auto permissions = QFile::permissions(destination);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(destination, permissions); });
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        auto *two = window.findChild<QAction *>("twoPanelsAction");
        if (!two->isChecked())
            two->trigger();
        auto *sourcePane = window.findChild<QWidget *>("filePanel0");
        auto *targetPane = window.findChild<QWidget *>("filePanel1");
        Navigate(*targetPane, target.path());
        Navigate(*sourcePane, source.path());
        auto *from = sourcePane->findChild<FileTable *>("archiveTable");
        auto *to = targetPane->findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(Row(*from, name) >= 0 && Row(*to, "folder") >= 0);
        to->sourcePanel->Set_AutoRefresh_Mode(false);
        QVERIFY(to->isVisible());
        const auto folder = [&] { return to->model()->index(Row(*to, "folder"), 0); };
        const auto blockedPermissions = QFile::ReadOwner | QFile::WriteOwner;
        bool accepted = false, dropped = false, blockAtDrop = false, permissionsChanged = false;
        bool blankTarget = false;
        QtPanelDragTransport transport(from, [&](QDrag &drag, Qt::DropActions actions) {
            const QPoint point = blankTarget ? QPoint(5, to->viewport()->height() - 5)
                                             : to->visualRect(folder()).center();
            QDragEnterEvent enter(DragEventPosition(point), actions, drag.mimeData(),
                                  Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(to->viewport(), &enter);
            accepted = enter.isAccepted();
            if (!accepted)
                return Qt::IgnoreAction;
            if (blockAtDrop)
                permissionsChanged = QFile::setPermissions(destination, blockedPermissions);
            QDropEvent drop(point, actions, drag.mimeData(), Qt::LeftButton, Qt::ControlModifier);
            QCoreApplication::sendEvent(to->viewport(), &drop);
            dropped = drop.isAccepted();
            return dropped ? drop.dropAction() : Qt::IgnoreAction;
        });
        from->sourcePanel->AttachDragServices(&transport);
        const auto detach = qScopeGuard([&] { from->sourcePanel->AttachDragServices(nullptr); });
        const auto start = [&] {
            from->setFocus();
            const int row = Row(*from, name);
            from->setCurrentCell(row, 0);
            from->selectRow(row);
            QCoreApplication::processEvents();
            from->dragStarter();
        };
        QVERIFY(QFile::setPermissions(destination, blockedPermissions));
        const QFileInfo blocked(destination);
        if (!blocked.isWritable() || blocked.isExecutable())
            QSKIP("Requires a writable directory without search access");
        start();
        QVERIFY(!accepted);
        QVERIFY(!folder().data(QtPanelList::DropHighlightedRole).toBool());
        QVERIFY(from->selectionModel()->isRowSelected(Row(*from, name), {}));
        QVERIFY(QFile::setPermissions(destination, permissions));
        blockAtDrop = true;
        start();
        QVERIFY(accepted);
        QVERIFY(permissionsChanged);
        QVERIFY(!dropped);
        QVERIFY(!folder().data(QtPanelList::DropHighlightedRole).toBool());
        QVERIFY(QFile::setPermissions(destination, permissions));
        QVERIFY(!QFileInfo::exists(QDir(destination).filePath(name)));
        blockAtDrop = false;
        start();
        QVERIFY(accepted && dropped);
        QTRY_COMPARE(SourceExtractFixture::readFile(QDir(destination).filePath(name)),
                     QByteArray("payload"));
        QCOMPARE(SourceExtractFixture::readFile(source.filePath(name)), QByteArray("payload"));

        Navigate(*targetPane, source.path());
        QTRY_VERIFY(to->isEnabled() && Row(*to, name) >= 0);
        blankTarget = true;
        QVERIFY(!to->indexAt(QPoint(5, to->viewport()->height() - 5)).isValid());
        start();
        QVERIFY(!accepted);
        QVERIFY(from->selectionModel()->isRowSelected(Row(*from, name), {}));
        blankTarget = false;
        start();
        QVERIFY(accepted && dropped);
        QTRY_COMPARE(SourceExtractFixture::readFile(source.filePath("folder/" + name)),
                     QByteArray("payload"));
    }

    void externalFilesUseArchiveConfirmation_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<int>("answer");
        QTest::newRow("table-yes") << false << int(QMessageBox::Yes);
        QTest::newRow("icons-yes") << true << int(QMessageBox::Yes);
        QTest::newRow("table-no") << false << int(QMessageBox::No);
        QTest::newRow("icons-cancel") << true << int(QMessageBox::Cancel);
    }
    void externalFilesUseArchiveConfirmation() {
        QFETCH(bool, icons);
        QFETCH(int, answer);
        QTemporaryDir root;
        const QString seed = root.filePath("original.txt");
        const QString source = root.filePath(QStringLiteral("добавлен с пробелом.txt"));
        for (const QString &path : {seed, source}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("archive UI bytes"), qint64(16));
        }
        const QString archive = root.filePath("target.zip");
        ArchiveBackend backend;
        QSignalSpy created(&backend, &ArchiveBackend::finished);
        QVERIFY(NativeArchiveFixture::WriteFiles(archive, {seed}));
        QFile before(archive);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QTRY_VERIFY(table->isEnabled() && Row(*table, "original.txt") >= 0);
        QVERIFY(table->dragEnabled());
        if (icons) {
            auto *action = window.findChild<QAction *>("viewMode0Action");
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
        }
        auto *iconList = window.findChild<QListView *>("iconList");
        QAbstractItemView *view = icons ? static_cast<QAbstractItemView *>(iconList) : table;
        QVERIFY(view->isVisible());
        QVERIFY(view->dragEnabled());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source)});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        bool questionShown = false;
        QString unexpectedError;
        QTimer respond;
        respond.setInterval(5);
        connect(&respond, &QTimer::timeout, &window, [&] {
            if (auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto *button = question->button(QMessageBox::StandardButton(answer))) {
                    questionShown = true;
                    button->click();
                } else if (auto *button = question->button(QMessageBox::Ok)) {
                    unexpectedError = question->text();
                    button->click();
                }
            }
        });
        respond.start();
        QDropEvent drop(QPointF(5, 5), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &drop);
        respond.stop();
        QVERIFY2(unexpectedError.isEmpty(), qPrintable(unexpectedError));
        QVERIFY(questionShown);
        const bool success = answer == QMessageBox::Yes;
        QCOMPARE(drop.isAccepted(), success);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!backend.busy());
        if (!success) {
            QVERIFY(before.open(QIODevice::ReadOnly));
            QCOMPARE(before.readAll(), original);
            return;
        }
        QTRY_VERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
        const QString destination = root.filePath("extracted");
        QVERIFY(QDir().mkpath(destination));
        ArchiveBackend verifier;
        QSignalSpy extracted(&verifier, &ArchiveBackend::finished);
        verifier.list(archive);
        QVERIFY(extracted.wait(30000));
        QVERIFY(extracted.takeFirst().first().toBool());
        verifier.extract(archive, destination, {QFileInfo(source).fileName()});
        QVERIFY(extracted.wait(30000));
        QVERIFY(extracted.takeFirst().first().toBool());
        QFile result(QDir(destination).filePath(QFileInfo(source).fileName()));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("archive UI bytes"));
    }

    void nativeSourceUsesActualTargetView_data() {
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("move");
        QTest::addColumn<int>("sourceIndex");
        QTest::addColumn<bool>("right");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<Qt::DropActions>("actions");
        QTest::addColumn<bool>("refreshTarget");
        QTest::addColumn<bool>("flat");
        const auto both = Qt::CopyAction | Qt::MoveAction;
        QTest::newRow("left-table-copy")
            << false << false << 0 << false << false << both << false << false;
        QTest::newRow("right-table-move")
            << false << true << 1 << false << false << both << false << false;
        QTest::newRow("left-icons-move")
            << true << true << 0 << false << false << both << false << false;
        QTest::newRow("right-icons-copy")
            << true << false << 1 << false << false << both << false << false;
        QTest::newRow("right-button-copy")
            << false << false << 0 << true << false << both << false << false;
        QTest::newRow("right-button-copy-only")
            << false << false << 0 << true << false << Qt::DropActions(Qt::CopyAction) << false
            << false;
        QTest::newRow("right-button-move-only")
            << true << true << 1 << true << false << Qt::DropActions(Qt::MoveAction) << false
            << false;
        QTest::newRow("right-button-cancel")
            << true << false << 1 << true << true << both << false << false;
        QTest::newRow("copy-after-target-refresh")
            << false << false << 0 << false << false << both << true << false;
        QTest::newRow("flat-folder-copy")
            << false << false << 0 << false << false << both << false << true;
    }
    void nativeSourceUsesActualTargetView() {
        QFETCH(bool, icons);
        QFETCH(bool, move);
        QFETCH(int, sourceIndex);
        QFETCH(bool, right);
        QFETCH(bool, cancel);
        QFETCH(Qt::DropActions, actions);
        QFETCH(bool, refreshTarget);
        QFETCH(bool, flat);
        QTemporaryDir root;
        const QString name = QStringLiteral("перенос с пробелом.txt");
        const QString source = root.filePath("source");
        const QString target = root.filePath("target");
        const QString subfolder = QStringLiteral("назначение с пробелом");
        const QString destination = QDir(target).filePath(flat ? "nested/" + subfolder : subfolder);
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(destination));
        QFile input(QDir(source).filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("UI drag bytes"), qint64(13));
        input.close();
        SavePanelPath(sourceIndex, ToUString(source),
                      qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SavePanelPath(1 - sourceIndex, ToUString(target),
                      qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.resize(980, 620);
        window.show();
        auto *two = window.findChild<QAction *>("twoPanelsAction");
        QTRY_VERIFY(two->isEnabled());
        if (!two->isChecked())
            two->trigger();
        auto *sourcePanel = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *targetPanel =
            window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourceIndex));
        QVERIFY(sourcePanel && targetPanel && targetPanel->isVisible());
        auto *sourceTable =
            dynamic_cast<FileTable *>(sourcePanel->findChild<FileTable *>("archiveTable"));
        auto *targetTable = targetPanel->findChild<FileTable *>("archiveTable");
        QVERIFY(sourceTable && sourceTable->dragStarter && targetTable);
        Navigate(*targetPanel, target);
        if (flat) {
            QTRY_VERIFY(targetTable->isEnabled() && Row(*targetTable, "nested") >= 0);
            targetTable->sourcePanel->ChangeFlatMode();
        }
        QTRY_VERIFY(targetTable->isEnabled() && Row(*targetTable, subfolder) >= 0);
        if (icons)
            window.findChild<QAction *>("viewMode0Action")->trigger();
        Navigate(*sourcePanel, source);
        QTRY_VERIFY(sourceTable->isEnabled() && Row(*sourceTable, name) >= 0);
        QVERIFY(sourceTable->sourcePanel);
        sourceTable->sourcePanel->Set_AutoRefresh_Mode(false);
        const int sourceRow = Row(*sourceTable, name);
        sourceTable->selectRow(sourceRow);
        sourceTable->setCurrentCell(sourceRow, 0);
        sourceTable->setFocus();
        QCoreApplication::processEvents();
        auto *targetIcons = targetPanel->findChild<QListView *>("iconList");
        QAbstractItemView *view =
            icons ? static_cast<QAbstractItemView *>(targetIcons) : targetTable;
        QVERIFY(view && view->isVisible());
        const int folderRow = Row(*targetTable, subfolder);
        const QModelIndex folder = targetTable->model()->index(folderRow, 0);
        const QPoint point = view->visualRect(folder).center();
        QVERIFY(view->viewport()->rect().contains(point));
        if (refreshTarget) {
            targetTable->sourcePanel->Set_AutoRefresh_Mode(false);
            for (int i = 0; i < 257; ++i)
                QVERIFY(SourceExtractFixture::writeFile(
                    QDir(target).filePath(QString("refresh-%1.txt").arg(i)), "fixture"));
        }
        const auto selection = targetTable->selectionModel()->selectedRows();
        bool entered = false, dropped = false, highlighted = false, sourceRefused = false;
        bool menuShown = false;
        bool canCopy = false, canMove = false, canArchive = false;
        QTimer chooseMenu;
        chooseMenu.setInterval(5);
        connect(&chooseMenu, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                auto *menu = qobject_cast<QMenu *>(widget);
                if (!menu || menu->objectName() != "dragContextMenu" || !menu->isVisible())
                    continue;
                const UInt32 copyCommand = NDragMenu::k_Copy_Base | NDragMenu::k_MenuFlag_Copy;
                const UInt32 moveCommand = NDragMenu::k_Copy_Base | NDragMenu::k_MenuFlag_Move;
                const UInt32 command =
                    cancel ? UInt32(NDragMenu::k_Cancel) : (move ? moveCommand : copyCommand);
                QAction *chosen = nullptr;
                menuShown = true;
                for (QAction *action : menu->actions()) {
                    if (action->isSeparator() || !action->isEnabled())
                        continue;
                    const UInt32 id = action->data().toUInt();
                    canCopy |= id == copyCommand;
                    canMove |= id == moveCommand;
                    canArchive |= id == (NDragMenu::k_AddToArc | NDragMenu::k_MenuFlag_Copy);
                    if (id == command)
                        chosen = action;
                }
                if (chosen) {
                    menu->setActiveAction(chosen);
                    QTest::keyClick(menu, Qt::Key_Return);
                } else
                    menu->close();
                return;
            }
        });
        if (right)
            chooseMenu.start();
        QtPanelDragTransport transport(sourceTable, [&](QDrag &drag, Qt::DropActions) {
            const auto modifiers = move ? Qt::ShiftModifier : Qt::ControlModifier;
            const auto button = right ? Qt::RightButton : Qt::LeftButton;
            QDragEnterEvent self(DragEventPosition(QPoint(5, 5)), Qt::CopyAction | Qt::MoveAction,
                                 drag.mimeData(), button, modifiers);
            QCoreApplication::sendEvent(sourceTable->viewport(), &self);
            sourceRefused = !self.isAccepted();
            QDragLeaveEvent leave;
            QCoreApplication::sendEvent(sourceTable->viewport(), &leave);
            QDragEnterEvent enter(DragEventPosition(point), actions, drag.mimeData(), button,
                                  modifiers);
            QCoreApplication::sendEvent(view->viewport(), &enter);
            entered = enter.isAccepted();
            highlighted = folder.data(Qt::UserRole + 19).toBool();
            QPoint dropPoint = point;
            if (refreshTarget) {
                if (targetTable->sourcePanel->RefreshListCtrl() != S_OK ||
                    targetTable->updatesEnabled())
                    return Qt::IgnoreAction;
                QDragMoveEvent blocked(point, actions, drag.mimeData(), button, modifiers);
                QCoreApplication::sendEvent(view->viewport(), &blocked);
                if (blocked.isAccepted() ||
                    !QTest::qWaitFor([&] { return targetTable->updatesEnabled(); }))
                    return Qt::IgnoreAction;
                const auto refreshed = targetTable->model()->index(Row(*targetTable, subfolder), 0);
                dropPoint = view->visualRect(refreshed).center();
                QDragMoveEvent resumed(dropPoint, actions, drag.mimeData(), button, modifiers);
                QCoreApplication::sendEvent(view->viewport(), &resumed);
                if (!resumed.isAccepted() ||
                    !refreshed.data(QtPanelList::DropHighlightedRole).toBool())
                    return Qt::IgnoreAction;
            }
            QDropEvent drop(dropPoint, actions, drag.mimeData(), button, modifiers);
            QCoreApplication::sendEvent(view->viewport(), &drop);
            dropped = drop.isAccepted();
            return dropped ? drop.dropAction() : Qt::IgnoreAction;
        });
        sourceTable->sourcePanel->AttachDragServices(&transport);
        if (right) {
            auto *viewport = sourceTable->viewport();
            const auto gesture = [&](const QPoint &start) {
                const QPoint moved = start + QPoint(QApplication::startDragDistance() + 1, 0);
                QMouseEvent press(QEvent::MouseButtonPress, start, viewport->mapToGlobal(start),
                                  Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                QCoreApplication::sendEvent(viewport, &press);
                QMouseEvent motion(QEvent::MouseMove, moved, viewport->mapToGlobal(moved),
                                   Qt::NoButton, Qt::RightButton, Qt::NoModifier);
                QCoreApplication::sendEvent(viewport, &motion);
                QMouseEvent release(QEvent::MouseButtonRelease, moved, viewport->mapToGlobal(moved),
                                    Qt::RightButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(viewport, &release);
            };
            const QPoint blank(viewport->width() / 2, viewport->height() - 5);
            QVERIFY(!sourceTable->indexAt(blank).isValid());
            gesture(blank);
            QVERIFY(!entered);
            sourceTable->setCurrentCell(sourceRow, 0);
            sourceTable->selectRow(sourceRow);
            gesture(sourceTable->visualRect(sourceTable->model()->index(sourceRow, 0)).center());
        } else
            sourceTable->sourcePanel->OnDrag(false);
        sourceTable->sourcePanel->AttachDragServices(nullptr);
        chooseMenu.stop();
        QVERIFY(sourceRefused);
        QVERIFY(entered && highlighted);
        QCOMPARE(dropped, !cancel);
        QCOMPARE(menuShown, right);
        if (right) {
            QCOMPARE(canCopy, actions.testFlag(Qt::CopyAction));
            QCOMPARE(canArchive, actions.testFlag(Qt::CopyAction));
            QCOMPARE(canMove, actions.testFlag(Qt::MoveAction));
        }
        QVERIFY(!targetTable->model()
                     ->index(Row(*targetTable, subfolder), 0)
                     .data(QtPanelList::DropHighlightedRole)
                     .toBool());
        QCOMPARE(targetTable->selectionModel()->selectedRows(), selection);
        if (cancel) {
            QTRY_COMPARE(window.focusWidget(), sourceTable);
            QVERIFY(PanelIndexSelected(sourceTable->model()->index(sourceRow, 0)));
            QVERIFY(QFileInfo::exists(input.fileName()));
            QVERIFY(!QFileInfo::exists(QDir(destination).filePath(name)));
            return;
        }
        QFile result(QDir(destination).filePath(name));
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("UI drag bytes"));
        if (flat)
            QVERIFY(!QFileInfo::exists(QDir(target).filePath(subfolder)));
        QCOMPARE(QFileInfo::exists(input.fileName()), !move);
        if (move)
            QTRY_COMPARE(Row(*sourceTable, name), -1);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("drag-%1-%2-%3x%4")
                                                          .arg(icons ? "icons" : "table")
                                                          .arg(sourceIndex)
                                                          .arg(size.width())
                                                          .arg(size.height())));
        }
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() == 3 && arguments[1] == "--copy-from-system-clipboard")
        return CopyExport(QApplication::clipboard()->mimeData(), arguments[2]) ? 0 : 1;
#ifdef Q_OS_LINUX
    if (arguments.size() == 5 && arguments[1] == "--receive-native-file-drop") {
        NativeDropReceiver receiver(arguments[2]);
        receiver.EnteredMarker = arguments[3];
        receiver.ReadPathsOnEnter = arguments[4] == "enter";
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        receiver.setGeometry(screen.right() - 225, screen.top() + 100, 220, 300);
        receiver.show();
        if (!QTest::qWaitForWindowExposed(&receiver) ||
            !SourceExtractFixture::writeFile(receiver.EnteredMarker + ".ready", "ready"))
            return 1;
        return app.exec();
    }
#endif
    DragDropUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "drag_drop_ui_tests.moc"
