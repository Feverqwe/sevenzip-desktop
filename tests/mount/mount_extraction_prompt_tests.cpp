#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/native_archive_fixture.h"
#include "localization/LangUtils.h"
#include "localization/qtlangbuttonbox.h"
#include "mount/qtarchivemountmanager.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include "mount/qtmountsettings.h"
#include "mount/qtsmbmountplatform.h"
#include "settings/RegistryUtils.h"
#include "ui/controls/qtdisclosure.h"
#include "ui/controls/qtsourcesummary.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/support/layout_inspection.h"
#include "ui/theme/qtcommandicons.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>
#include <atomic>
#include <future>

namespace {
class MountTestLanguage final {
    QTemporaryDir settings_;

  public:
    explicit MountTestLanguage(const QString &language) {
        SaveRegLang(language == "ru" ? UString(L"ru") : UString(L"-"),
                    settings_.filePath("language.ini"));
        ReloadLang(settings_.filePath("language.ini"));
    }
    ~MountTestLanguage() {
        SaveRegLang(L"-", settings_.filePath("language.ini"));
        ReloadLang(settings_.filePath("language.ini"));
    }
};

HRESULT OpenMountResource(const QString &path, const QString &format, CMyComPtr<IUnknown> &resource,
                          std::shared_ptr<ArchiveNativeOpen> &reader) {
    auto open = std::async(std::launch::async, [&] {
        return ArchiveBackend::openNativeArchive(NativeArchiveFixture::Catalog(), path, format,
                                                 nullptr, reader, nullptr);
    });
    while (open.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        QTest::qWait(1);
    const HRESULT result = open.get();
    if (result != S_OK)
        return result;
    auto *owner = new QtArchiveFileResource(path);
    resource = owner;
    owner->AttachNativeOpen(reader);
    owner->UseNativeMetadata();
    return S_OK;
}

bool RemoveThroughContextMenu(QWidget &entry) {
    bool invoked = false;
    QTimer choose;
    QObject::connect(&choose, &QTimer::timeout, &entry, [&] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            invoked = true;
            menu->setActiveAction(entry.findChild<QAction *>("removeMountAction"));
            QTest::keyClick(menu, Qt::Key_Return);
        }
    });
    choose.start(5);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &entry, [] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
            menu->close();
    });
    timeout.start(1000);
    const QPoint point = entry.rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, entry.mapToGlobal(point));
    QApplication::sendEvent(&entry, &event);
    return invoked;
}
} // namespace

class MountExtractionPromptTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { UiInspection::configureReviewAppearance(); }
    void cacheControlsFollowReader_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("keyboard");
        for (const QString &language : {QString("en"), QString("ru")}) {
            QTest::newRow(qPrintable(language + " direct")) << QString("tar") << language << false;
            QTest::newRow(qPrintable(language + " cache help"))
                << QString("zip") << language << false;
            QTest::newRow(qPrintable(language + " cache F1")) << QString("zip") << language << true;
        }
    }
    void cacheControlsFollowReader() {
        QFETCH(QString, format);
        QFETCH(QString, language);
        QFETCH(bool, keyboard);
        MountTestLanguage catalog(language);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source." + format);
        QVERIFY(NativeArchiveFixture::Write(path, {{"file.zip", "raw bytes"}}, format));
        CMyComPtr<IUnknown> resource;
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(OpenMountResource(path, format, resource, reader), S_OK);
        bool inspected = false, correct = false, helpShown = false, helpKeptDialog = false;
        QTimer inspect;
        connect(&inspect, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "archiveMountSettings")
                return;
            auto *box = dialog->findChild<QDialogButtonBox *>();
            auto *quota = dialog->findChild<QSpinBox *>("mountCacheLimit");
            if (!box || !quota)
                return;
            const auto buttons = box->buttons();
            QPushButton *start = nullptr;
            for (auto *button : buttons)
                if (box->buttonRole(button) == QDialogButtonBox::AcceptRole)
                    start = qobject_cast<QPushButton *>(button);
            if (!start || !start->isEnabled())
                return;
            inspect.stop();
            auto *help = box->button(QDialogButtonBox::Help);
            correct =
                quota->isVisible() == (format == "zip") && help->isVisible() == (format == "zip");
            dialog->resize(420, dialog->sizeHint().height());
            inspected = UiInspection::writeReport(
                *dialog, QStringLiteral("mount-reader-%1-%2").arg(format, language));
            if (format == "zip") {
                quota->setValue(64);
                if (keyboard) {
                    dialog->activateWindow();
                    if (QTest::qWaitForWindowActive(dialog, 1000))
                        QTest::keySequence(dialog, QKeySequence(QKeySequence::HelpContents));
                } else
                    help->click();
                auto *window = dialog->findChild<QDialog *>("helpWindow");
                auto *browser = window ? window->findChild<QTextBrowser *>("helpBrowser") : nullptr;
                helpShown =
                    window && window->isVisible() && browser && !browser->toPlainText().isEmpty();
                helpKeptDialog = dialog->isVisible() && dialog->result() != QDialog::Accepted;
                if (window) {
                    inspected = inspected &&
                                UiInspection::writeReport(
                                    *window, QStringLiteral("mount-cache-help-%1").arg(language));
                    window->close();
                }
            }
            dialog->accept();
        });
        inspect.start(5);
        const auto options = QtAskMountOptions(nullptr, "folder/", {}, resource);
        QVERIFY(options.Accepted);
        QVERIFY(options.View);
        QVERIFY(inspected && correct);
        QCOMPARE(options.Quota, format == "zip" ? quint64(64) * 1024 * 1024 : quint64(0));
        if (format == "zip")
            QVERIFY(helpShown && helpKeptDialog);
    }
    void cancellingReaderCheckKeepsEventLoopResponsive_data() { setupGeometry_data(); }
    void cancellingReaderCheckKeepsEventLoopResponsive() {
        QFETCH(int, width);
        QFETCH(QString, language);
        MountTestLanguage catalog(language);
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.tar");
        QVERIFY(NativeArchiveFixture::Write(path, {{"data.bin", "raw bytes"}}, "tar"));
        CMyComPtr<IUnknown> resource;
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(OpenMountResource(path, "tar", resource, reader), S_OK);
        std::unique_lock<std::mutex> busy(reader->itemStreamMutex());
        bool cancelled = false, released = false, progressVisible = false, fits = true;
        QString initialElapsed, laterElapsed;
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                dialog->resize(width, dialog->sizeHint().height());
                auto *progress = dialog->findChild<QProgressBar *>("mountPreparationProgress");
                auto *elapsed = dialog->findChild<QLabel *>("mountPreparationElapsed");
                progressVisible = progress && progress->isVisible() && progress->maximum() == 0;
                initialElapsed = elapsed ? elapsed->text() : QString();
                for (const auto *label : dialog->findChildren<QLabel *>())
                    if (label->isVisible())
                        fits &= UiInspection::fullyVisible(*label, *dialog) &&
                                UiInspection::labelFits(*label);
                fits &= dialog->width() <= width;
                fits &= UiInspection::writeReport(
                    *dialog, QStringLiteral("mount-preparing-%1-%2").arg(language).arg(width));
            }
            QTimer::singleShot(1200, this, [&, dialog] {
                if (dialog) {
                    auto *elapsed = dialog->findChild<QLabel *>("mountPreparationElapsed");
                    laterElapsed = elapsed ? elapsed->text() : QString();
                    dialog->reject();
                    cancelled = dialog->isVisible();
                }
                released = true;
                busy.unlock();
            });
        });
        const auto options = QtAskMountOptions(nullptr, "folder/", {}, resource);
        QVERIFY(!options.Accepted);
        QVERIFY(cancelled && released);
        QVERIFY(progressVisible && fits);
        QVERIFY(!initialElapsed.isEmpty() && initialElapsed != laterElapsed);
    }
    void failedReaderCheckReportsErrorAndCannotMount() {
        CMyComPtr<IUnknown> resource = new QtArchiveFileResource(QStringLiteral("unopened.img"));
        std::shared_ptr<ArchiveReadOnlyView> view;
        QString error;
        QCOMPARE(ArchiveBackend::createReadOnlyView(resource, 0, {}, view, error), E_NOINTERFACE);
        QVERIFY(!error.isEmpty());
        QObject context;
        bool reported = false, blocked = false;
        QTimer inspect;
        connect(&inspect, &QTimer::timeout, &context, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "archiveMountSettings")
                return;
            for (const auto *label : dialog->findChildren<QLabel *>())
                if (label->isVisible() && label->text() == error)
                    reported = true;
            if (!reported)
                return;
            inspect.stop();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            for (auto *button : buttons->buttons())
                if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                    blocked = !button->isEnabled();
            dialog->accept();
            blocked = blocked && dialog->isVisible();
            dialog->reject();
        });
        inspect.start(5);
        QTimer::singleShot(2000, &context, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        const auto options = QtAskMountOptions(nullptr, "unopened", {}, resource);
        QVERIFY(!options.Accepted);
        QVERIFY(reported && blocked);
    }
    void unmountContinuationKeepsItsRequestedOwner_data() {
        QTest::addColumn<bool>("tab");
        QTest::newRow("tab-close-follows-its-continuation") << true;
        QTest::newRow("window-close-stays-with-original-window") << false;
    }
    void unmountContinuationKeepsItsRequestedOwner() {
        QFETCH(bool, tab);
        const auto unavailable = QtSmbMountPlatform::unavailableReason();
        if (!unavailable.isEmpty())
            QSKIP(qPrintable(unavailable));
        QWidget owner;
        QWidget receiver;
        owner.show();
        receiver.show();
        QtArchiveMountManager manager(&owner);
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                dialog && dialog->objectName() == "archiveMountSettings")
                dialog->accept();
            else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                for (auto *button : box->buttons())
                    if (box->buttonRole(button) == QMessageBox::AcceptRole) {
                        button->click();
                        return;
                    }
        });
        answer.start(5);
        // As in stopped-record coverage, an absent reader exercises the
        // Preparing -> Stopped lifecycle without starting an OS mount.
        manager.mount(nullptr, "Synthetic preparing source", {});
        QVERIFY(manager.hasActiveSessions());
        int continued = 0;
        std::function<void()> close;
        if (tab)
            close = [&] { ++continued; };
        QVERIFY(!manager.canCloseOwner(close));
        manager.rebindOwner(receiver);
        QTRY_VERIFY(!manager.hasActiveSessions());
        if (tab) {
            QTRY_COMPARE(continued, 1);
            QVERIFY(owner.isVisible());
        } else
            QTRY_VERIFY(!owner.isVisible());
        QVERIFY(receiver.isVisible());
        QTest::qWait(20);
        QCOMPARE(continued, tab ? 1 : 0);
    }
    void removesOnlySelectedStoppedRecord() {
        const auto unavailable = QtSmbMountPlatform::unavailableReason();
        if (!unavailable.isEmpty())
            QSKIP(qPrintable(unavailable));
        QTemporaryDir fixture;
        const QString first = fixture.filePath("first.img");
        const QString second = fixture.filePath("second.img");
        const QString longLabel = second + QString(800, u'δ');
        for (const auto &path : {first, second}) {
            QFile source(path);
            QVERIFY(source.open(QIODevice::WriteOnly));
            QCOMPARE(source.write("source bytes"), qint64(12));
        }
        QWidget owner;
        QtArchiveMountManager manager(&owner);
        const auto rowFor = [&](const QString &path) -> QtMountRow * {
            for (auto *row : manager.findChildren<QtMountRow *>())
                if (row->findChild<QWidget *>("mountSourceSummary")->toolTip() == path)
                    return row;
            return nullptr;
        };
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                dialog && dialog->objectName() == "archiveMountSettings")
                dialog->accept();
        });
        answer.start(5);
        for (const auto &label : {first, longLabel}) {
            // Missing readers fail before starting a server or OS mount.
            manager.mount(nullptr, label, {});
            auto *row = rowFor(label);
            QVERIFY(row);
            auto *remove = row->findChild<QAction *>("removeMountAction");
            QVERIFY(remove && !remove->isEnabled());
            remove->trigger();
            QCOMPARE(rowFor(label), row);
            auto *session = manager.findChildren<QtArchiveMountSession *>().last();
            session->entryIssue(0, quint32(label == first ? E_FAIL : S_FALSE),
                                label == first ? "first read issue" : "second read issue", -1);
            session->entryIssue(0, quint32(E_ABORT), "cancelled read", -1);
            QTRY_COMPARE(session->status(), QtArchiveMountSession::Status::Stopped);
            QVERIFY(remove->isEnabled());
            QTRY_VERIFY_WITH_TIMEOUT(
                row->hasFocus() || row->isAncestorOf(QApplication::focusWidget()), 3000);
            for (const char *name :
                 {"openMountFolderButton", "copyMountPathButton", "unmountButton"}) {
                auto *button = row->findChild<QPushButton *>(name);
                QVERIFY(button && button->isVisible() && !button->isEnabled());
            }
            auto *details = row->findChild<QPlainTextEdit *>("mountDetails");
            QVERIFY(details->toPlainText().contains(label == first ? "first read issue"
                                                                   : "second read issue"));
            QVERIFY(!details->toPlainText().contains(label == first ? "second read issue"
                                                                    : "first read issue"));
            QVERIFY(!details->toPlainText().contains("cancelled read"));
        }
        answer.stop();
        QCOMPARE(manager.findChildren<QtMountRow *>().size(), 2);
        QVERIFY(UiInspection::writeReport(manager, "mount-manager-long-source"));
        QVERIFY(RemoveThroughContextMenu(*rowFor(first)));
        QTRY_COMPARE(manager.findChildren<QtMountRow *>().size(), 1);
        auto *remainingRow = rowFor(longLabel);
        QVERIFY(remainingRow);
        auto *details = remainingRow->findChild<QPlainTextEdit *>("mountDetails");
        QVERIFY(details->toPlainText().contains("second read issue"));
        QVERIFY(!details->toPlainText().contains("first read issue"));
        auto *remaining = manager.findChildren<QtArchiveMountSession *>().last();
        remaining->entryIssue(0, quint32(E_FAIL), "later second read issue", -1);
        QVERIFY(details->toPlainText().contains("later second read issue"));
        auto *remove = remainingRow->findChild<QPushButton *>("removeMountButton");
        QVERIFY(remove && remove->isVisible());
        remove->click();
        QTRY_VERIFY(manager.findChildren<QtMountRow *>().isEmpty());
        QVERIFY(manager.findChild<QLabel *>("mountEmpty")->isVisible());
        QVERIFY(manager.canCloseOwner());
        for (const auto &path : {first, second}) {
            QFile source(path);
            QVERIFY(source.open(QIODevice::ReadOnly));
            QCOMPARE(source.readAll(), QByteArray("source bytes"));
        }
    }
    void managerActionsGeometry_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("language");
        for (const QString &language : {QStringLiteral("en"), QStringLiteral("ru")})
            for (int width : {420, 640, 780})
                QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(language).arg(width)))
                    << width << language;
    }
    void managerActionsGeometry() {
        QFETCH(int, width);
        QFETCH(QString, language);
        MountTestLanguage catalog(language);
        QDialog window;
        auto *footer = new QtLangButtonBox(QDialogButtonBox::Close, &window);
        auto *body = QtCreateDialogLayout(window, *footer, QtDialogContent::Scrollable);
        auto *row = new QtMountRow(QtCommandSymbol(QtCommandIcon::Archive), &window);
        QtMountRowData data;
        data.name = QStringLiteral("Archive with a long name 空.7z");
        data.sourcePath = "/" + QStringLiteral("long source folder 空/").repeated(20) + data.name;
        data.mountPath = QStringLiteral("/mounts/Archive 空");
        data.status = QCoreApplication::translate("QtArchiveMountManager", "Mounted read-only");
        data.error = language == "ru"
                         ? QStringLiteral("Папку использует другое приложение. Закройте открытые "
                                          "файлы и повторите попытку.")
                         : QStringLiteral("The folder is busy. Close open files and try again.");
        data.report = data.sourcePath + '\n' + data.mountPath + '\n' + data.error;
        data.canOpen = data.canUnmount = data.canForce = true;
        row->SetData(data);
        body->addWidget(row);
        window.resize(width, width <= 420 ? 300 : 400);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *scroll = window.findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(scroll);
        QSignalSpy requested(row, &QtMountRow::Requested);
        for (const char *name : {"openMountFolderButton", "copyMountPathButton", "unmountButton",
                                 "forceUnmountButton"}) {
            auto *button = row->findChild<QPushButton *>(name);
            QVERIFY(button && button->isVisible());
            scroll->ensureWidgetVisible(button, 0, 0);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*button, window));
            QVERIFY(UiInspection::fullyVisible(*footer, window));
        }
        auto *copy = row->findChild<QPushButton *>("copyMountPathButton");
        scroll->ensureWidgetVisible(copy, 0, 0);
        QTest::mouseClick(copy, Qt::LeftButton);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested[0][0].value<QtMountRow::Action>(), QtMountRow::Action::CopyPath);
        row->findChild<QtDisclosure *>()->setExpanded(true);
        QApplication::processEvents();
        auto *report = row->findChild<QPlainTextEdit *>("mountDetails");
        QCOMPARE(report->toPlainText(), data.report);
        QVERIFY(report->isReadOnly());
        QVERIFY(UiInspection::fullyVisible(*footer, window));
        row->findChild<QtDisclosure *>()->setExpanded(false);
        scroll->verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        QTRY_VERIFY(
            UiInspection::bounds(*row->findChild<QPushButton *>("openMountFolderButton"), window)
                    .top() -
                UiInspection::bounds(*row->findChild<QtDisclosure *>()->button(), window)
                    .bottom() <=
            window.fontMetrics().lineSpacing() * 2);
        QVERIFY(UiInspection::writeReport(
            window, QStringLiteral("mount-manager-actions-%1-%2").arg(language).arg(width)));
        auto *open = row->findChild<QPushButton *>("openMountFolderButton");
        open->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(open->hasFocus(), 3000);
        data.error.clear();
        data.report.clear();
        data.canOpen = data.canUnmount = data.canForce = false;
        data.status = "Stopping";
        row->SetData(data);
        QVERIFY(row->hasFocus());
        data.canRemove = true;
        data.status = "Disconnected";
        row->SetData(data);
        QVERIFY(row->hasFocus());
        row->FocusPrimaryAction();
        QVERIFY(row->findChild<QPushButton *>("removeMountButton")->hasFocus());
    }
    void decision_data() {
        QTest::addColumn<int>("action");
        QTest::addColumn<bool>("unverified");
        for (bool unverified : {false, true}) {
            const QString mode = unverified ? "recovery " : "extraction ";
            QTest::newRow(qPrintable(mode + "Return cancels")) << 0 << unverified;
            QTest::newRow(qPrintable(mode + "Escape cancels")) << 1 << unverified;
            QTest::newRow(qPrintable(mode + "Close cancels")) << 2 << unverified;
            QTest::newRow(qPrintable(mode + "explicit consent")) << 3 << unverified;
        }
    }
    void decision() {
        QFETCH(int, action);
        QFETCH(bool, unverified);
        const auto decision = std::make_shared<ArchiveMountDecision>();
        std::atomic_bool cancelled{false};
        auto answer = std::async(std::launch::async, [&] { return decision->wait(cancelled); });
        QtMountExtractionPrompt prompt("Synthetic image", "folder/данные 空 <file>.bin", 200000,
                                       decision, nullptr, unverified);
        prompt.show();
        QTest::qWait(10);
        if (action == 0)
            QTest::keyClick(&prompt, Qt::Key_Return);
        else if (action == 1)
            QTest::keyClick(&prompt, Qt::Key_Escape);
        else if (action == 2)
            prompt.close();
        else
            QTest::mouseClick(prompt.findChild<QPushButton *>("extractForMountButton"),
                              Qt::LeftButton);
        QElapsedTimer timer;
        timer.start();
        while (answer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               timer.elapsed() < 1000)
            QTest::qWait(5);
        // Ensure a broken UI signal cannot leave the test's worker blocked.
        if (answer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
            cancelled = true;
        QVERIFY(!cancelled);
        QCOMPARE(answer.get(), action == 3);
    }
    void destructionCancels() {
        const auto decision = std::make_shared<ArchiveMountDecision>();
        {
            QtMountExtractionPrompt prompt("Synthetic image", "file.bin", 10, decision);
        }
        const std::atomic_bool cancelled{false};
        QVERIFY(!decision->wait(cancelled));
    }
    void geometry_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("unverified");
        for (bool unverified : {false, true})
            for (const QString &language : {QStringLiteral("en"), QStringLiteral("ru")})
                for (int width : {640, 420})
                    QTest::newRow(qPrintable(
                        QStringLiteral("%1 %2 %3").arg(language).arg(width).arg(unverified)))
                        << width << language << unverified;
    }
    void geometry() {
        QFETCH(int, width);
        QFETCH(QString, language);
        QFETCH(bool, unverified);
        const QLocale previous;
        const auto restoreLocale = qScopeGuard([&] { QLocale::setDefault(previous); });
        QLocale::setDefault(QLocale(language));
        MountTestLanguage catalog(language);
        auto decision = std::make_shared<ArchiveMountDecision>();
        QtMountExtractionPrompt prompt(
            "Synthetic image with spaces.img",
            "folder/длинное имя с пробелами/данные 空 для внешнего приложения.bin", 200000,
            decision, nullptr, unverified);
        prompt.resize(width, width <= 420 ? 300 : 420);
        prompt.show();
        QTest::qWait(10);
        QVERIFY(prompt.width() <= width);
        auto *scroll = prompt.findChild<QScrollArea *>("dialogContentScroll");
        auto *footer = prompt.findChild<QDialogButtonBox *>();
        QVERIFY(scroll && footer);
        for (auto *label : prompt.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                scroll->ensureWidgetVisible(label, 0, 0);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*label, prompt));
                QVERIFY(UiInspection::labelFits(*label));
                QVERIFY(UiInspection::fullyVisible(*footer, prompt));
            }
        }
        const auto buttons = prompt.findChildren<QPushButton *>();
        for (const auto *button : buttons)
            QVERIFY(UiInspection::fullyVisible(*button, prompt));
        for (int first = 0; first < buttons.size(); ++first)
            for (int second = first + 1; second < buttons.size(); ++second)
                QVERIFY(UiInspection::separate(*buttons[first], *buttons[second], prompt));
        QVERIFY(UiInspection::writeReport(prompt,
                                          QStringLiteral("mount-%1-%2-%3")
                                              .arg(unverified ? "recovery" : "extraction", language)
                                              .arg(width)));
        prompt.reject();
    }
    void setupGeometry_data() {
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("language");
        QTest::newRow("EN normal") << 640 << QStringLiteral("en");
        QTest::newRow("EN compact") << 420 << QStringLiteral("en");
        QTest::newRow("RU normal") << 640 << QStringLiteral("ru");
        QTest::newRow("RU compact") << 420 << QStringLiteral("ru");
    }
    void setupGeometry() {
        QFETCH(int, width);
        QFETCH(QString, language);
        MountTestLanguage catalog(language);
        bool inspected = false, fits = true;
        QTimer inspect;
        connect(&inspect, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->objectName() != "archiveMountSettings")
                return;
            inspect.stop();
            dialog->resize(width, dialog->sizeHint().height());
            QTest::qWait(10);
            fits = dialog->width() <= width;
            for (const auto *label : dialog->findChildren<QLabel *>())
                if (label->isVisible())
                    fits = fits && UiInspection::fullyVisible(*label, *dialog) &&
                           UiInspection::labelFits(*label);
            for (const auto *button : dialog->findChildren<QPushButton *>())
                if (button->isVisible())
                    fits = fits && UiInspection::fullyVisible(*button, *dialog);
            inspected = UiInspection::writeReport(
                *dialog, QStringLiteral("mount-settings-%1-%2").arg(language).arg(width));
            dialog->reject();
        });
        inspect.start(5);
        const auto options = QtAskMountOptions(
            nullptr, "Synthetic image/длинное имя с пробелами/данные 空.img", "SyntheticOnly");
        QVERIFY(!options.Accepted);
        QVERIFY(inspected);
        QVERIFY(fits);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(MountExtractionPromptTests)
#include "mount_extraction_prompt_tests.moc"
