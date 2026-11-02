#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/native_archive_fixture.h"
#include "mount/qtarchivemountmanager.h"
#include "mount/qtarchivemountsession.h"
#include "mount/qtmountextractionprompt.h"
#include "mount/qtmountsettings.h"
#include "mount/qtsmbmountplatform.h"
#include "ui/support/layout_inspection.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTranslator>
#include <QTreeWidget>
#include <QtTest>
#include <atomic>
#include <future>

namespace {
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

bool RemoveThroughContextMenu(QTreeWidget &list) {
    bool invoked = false;
    QTimer choose;
    QObject::connect(&choose, &QTimer::timeout, &list, [&] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            invoked = true;
            menu->setActiveAction(list.findChild<QAction *>("removeMountAction"));
            QTest::keyClick(menu, Qt::Key_Return);
        }
    });
    choose.start(5);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &list, [] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
            menu->close();
    });
    timeout.start(1000);
    const QPoint point = list.visualItemRect(list.currentItem()).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, list.viewport()->mapToGlobal(point));
    QApplication::sendEvent(list.viewport(), &event);
    return invoked;
}
} // namespace

class MountExtractionPromptTests final : public QObject {
    Q_OBJECT
  private slots:
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
        QTranslator catalog;
        if (language == "ru") {
            QVERIFY(catalog.load(":/translations/sevenzip_ru.qm"));
            qApp->installTranslator(&catalog);
        }
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
                if (keyboard)
                    QTest::keyClick(dialog, Qt::Key_F1);
                else
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
    void cancellingReaderCheckKeepsEventLoopResponsive() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("source.tar");
        QVERIFY(NativeArchiveFixture::Write(path, {{"data.bin", "raw bytes"}}, "tar"));
        CMyComPtr<IUnknown> resource;
        std::shared_ptr<ArchiveNativeOpen> reader;
        QCOMPARE(OpenMountResource(path, "tar", resource, reader), S_OK);
        std::unique_lock<std::mutex> busy(reader->itemStreamMutex());
        bool cancelled = false, released = false;
        QTimer::singleShot(0, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                dialog->reject();
                cancelled = dialog->isVisible();
            }
            QTimer::singleShot(10, this, [&] {
                released = true;
                busy.unlock();
            });
        });
        const auto options = QtAskMountOptions(nullptr, "folder/", {}, resource);
        QVERIFY(!options.Accepted);
        QVERIFY(cancelled && released);
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
        auto *remove = manager.findChild<QAction *>("removeMountAction");
        auto *list = manager.findChild<QTreeWidget *>("mountList");
        auto *details = manager.findChild<QPlainTextEdit *>("mountDetails");
        QVERIFY(remove && list && details);
        QVERIFY(!remove->isEnabled());
        const int initialWidth = list->columnWidth(0);
        QTimer answer;
        connect(&answer, &QTimer::timeout, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                dialog && dialog->objectName() == "archiveMountSettings")
                dialog->accept();
        });
        answer.start(5);
        for (const auto &label : {first, longLabel}) {
            // Missing retained readers fail before starting a server or OS mount.
            manager.mount(nullptr, label, {});
            QVERIFY(!remove->isEnabled());
            auto *session = manager.findChildren<QtArchiveMountSession *>().last();
            session->entryIssue(0, quint32(label == first ? E_FAIL : S_FALSE),
                                label == first ? "first read issue" : "second read issue", -1);
            session->entryIssue(0, quint32(E_ABORT), "cancelled read", -1);
            QTRY_COMPARE(session->status(), QtArchiveMountSession::Status::Stopped);
            QVERIFY(remove->isEnabled());
            QVERIFY(details->toPlainText().contains(label == first ? "first read issue"
                                                                   : "second read issue"));
            QVERIFY(!details->toPlainText().contains(label == first ? "second read issue"
                                                                    : "first read issue"));
            QVERIFY(!details->toPlainText().contains("cancelled read"));
        }
        answer.stop();
        QCOMPARE(list->topLevelItemCount(), 2);
        QCOMPARE(list->columnWidth(0), initialWidth);
        QVERIFY(initialWidth < list->fontMetrics().horizontalAdvance(longLabel));
        QVERIFY(UiInspection::writeReport(manager, "mount-manager-long-source"));
        list->header()->resizeSection(0, initialWidth + 80);
        list->setCurrentItem(list->topLevelItem(0));
        QVERIFY(details->toPlainText().contains("first read issue"));
        QVERIFY(!details->toPlainText().contains("second read issue"));
        QVERIFY(RemoveThroughContextMenu(*list));
        QCOMPARE(list->topLevelItemCount(), 1);
        QCOMPARE(list->currentItem()->text(0), longLabel);
        QCOMPARE(list->columnWidth(0), initialWidth + 80);
        QVERIFY(remove->isEnabled());
        QVERIFY(details->toPlainText().contains("second read issue"));
        QVERIFY(!details->toPlainText().contains("first read issue"));
        const auto sessions = manager.findChildren<QtArchiveMountSession *>();
        auto *remaining = sessions.last();
        remaining->entryIssue(0, quint32(E_FAIL), "later second read issue", -1);
        QVERIFY(details->toPlainText().contains("later second read issue"));
        QVERIFY(RemoveThroughContextMenu(*list));
        QCOMPARE(list->topLevelItemCount(), 0);
        QVERIFY(manager.findChild<QPlainTextEdit *>("mountDetails")->toPlainText().isEmpty());
        QVERIFY(!remove->isEnabled());
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
            for (int width : {640, 780})
                QTest::newRow(qPrintable(QStringLiteral("%1 %2").arg(language).arg(width)))
                    << width << language;
    }
    void managerActionsGeometry() {
        QFETCH(int, width);
        QFETCH(QString, language);
        QTranslator catalog;
        if (language == "ru") {
            QVERIFY(catalog.load(":/translations/sevenzip_ru.qm"));
            qApp->installTranslator(&catalog);
        }
        QWidget owner;
        QtArchiveMountManager manager(&owner);
        // Exercise the width needed when the busy-mount force action is
        // exposed, without requiring a privileged OS mount for layout checks.
        const auto buttons = manager.findChildren<QPushButton *>();
        for (auto *button : buttons)
            button->show();
        manager.resize(width, 400);
        manager.show();
        QTest::qWait(10);
        for (const auto *button : buttons) {
            QVERIFY(button->isVisible());
            QVERIFY(UiInspection::fullyVisible(*button, manager));
        }
        const auto *open = manager.findChild<QPushButton *>("openMountFolderButton");
        for (const char *name : {"copyMountPathButton", "unmountButton", "forceUnmountButton"}) {
            const auto *button = manager.findChild<QPushButton *>(name);
            QCOMPARE(button->mapTo(&manager, QPoint()).y(), open->mapTo(&manager, QPoint()).y());
        }
        for (int first = 0; first < buttons.size(); ++first)
            for (int second = first + 1; second < buttons.size(); ++second)
                QVERIFY(UiInspection::separate(*buttons[first], *buttons[second], manager));
        QVERIFY(UiInspection::writeReport(
            manager, QStringLiteral("mount-manager-actions-%1-%2").arg(language).arg(width)));
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
        QTranslator catalog;
        if (language == "ru") {
            QVERIFY(catalog.load(":/translations/sevenzip_ru.qm"));
            qApp->installTranslator(&catalog);
        }
        auto decision = std::make_shared<ArchiveMountDecision>();
        QtMountExtractionPrompt prompt(
            "Synthetic image with spaces.img",
            "folder/длинное имя с пробелами/данные 空 для внешнего приложения.bin", 200000,
            decision, nullptr, unverified);
        prompt.resize(width, prompt.sizeHint().height());
        prompt.show();
        QTest::qWait(10);
        QVERIFY(prompt.width() <= width);
        for (const auto *label : prompt.findChildren<QLabel *>()) {
            if (label->isVisible()) {
                QVERIFY(UiInspection::fullyVisible(*label, prompt));
                QVERIFY(UiInspection::labelFits(*label));
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
        QTranslator catalog;
        if (language == "ru") {
            QVERIFY(catalog.load(":/translations/sevenzip_ru.qm"));
            qApp->installTranslator(&catalog);
        }
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
QTEST_MAIN(MountExtractionPromptTests)
#include "mount_extraction_prompt_tests.moc"
