#include "archive/codecs/archivecodeccatalog.h"
#include "archive/codecs/archivecodecprobe.h"
#include "devices/deviceselectiondialog.h"
#include "dialogs/copy/alias.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "formats/exfat/support/exfat_fixture.h"
#include "localization/LangUtils.h"
#include "localization/qtlanguageservices.h"
#include "ui/support/layout_inspection.h"
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>
#include <cstring>

namespace {
class CDeviceBytes final : public DeviceReadHandle {
  public:
    QByteArray Bytes;
    explicit CDeviceBytes(QByteArray bytes) : Bytes(std::move(bytes)) {}
    qint32 readAt(quint64 offset, void *buffer, quint32 size, quint32 &read) override {
        read = 0;
        if (offset > quint64(Bytes.size()) || size > quint64(Bytes.size()) - offset)
            return E_FAIL;
        memcpy(buffer, Bytes.constData() + offset, size);
        read = size;
        return S_OK;
    }
};
ExternalDevice Device(const QByteArray &bytes) {
    return {"private-device",
            QStringLiteral("USB диск 空"),
            "fixture",
            quint64(bytes.size()),
            512,
            false};
}
DeviceSelectionServices Services(const std::shared_ptr<CDeviceBytes> &bytes) {
    auto device = Device(bytes->Bytes);
    return {[device](QString &) {
                auto busy = device;
                busy.id = "busy-fixture";
                busy.mounted = true;
                return QList<ExternalDevice>{busy, device};
            },
            [bytes](const ExternalDevice &, const auto &) {
                return DeviceAccessResult{DeviceAccessStatus::Ready, {}, bytes};
            }};
}
QTreeWidget *Items(DeviceSelectionDialog &dialog) {
    return dialog.findChild<QTreeWidget *>("deviceItems");
}
QPushButton *Open(DeviceSelectionDialog &dialog) {
    return dialog.findChild<QPushButton *>("deviceOpenButton");
}
int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).isValid() &&
            table.model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
} // namespace

class DeviceUiTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
    QByteArray fixture_;
    QTemporaryDir settings_;
    QByteArray previousSettings_;
    std::shared_ptr<const ArchiveCodecCatalog> catalog_;
  private slots:
    void initTestCase() {
        previousSettings_ = qgetenv("SEVENZIP_SETTINGS_PATH");
        QVERIFY(settings_.isValid());
        qputenv("SEVENZIP_SETTINGS_PATH", settings_.filePath("settings.ini").toUtf8());
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; device UI integration unverified");
#else
        module_ = QStringLiteral(SEVENZIP_CODEC_MODULE);
        engine_ = QFileInfo(module_).dir().filePath(
#ifdef Q_OS_WIN
            "7z.exe"
#else
            "7z"
#endif
        );
        ArchiveCodecProbe probe;
        bool done = false;
        QVERIFY(probe.Start(module_, [&](auto result) {
            catalog_ = result.catalog;
            done = true;
        }));
        // The probe deadline bounds process discovery; native DLL catalog loading
        // completes asynchronously and can take longer on Windows.
        QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
        QVERIFY(catalog_);
        QCOMPARE(catalog_->result(), S_OK);
        QProcess gzip;
        gzip.start(engine_, {"x", "-so", "--", QStringLiteral(DEVICE_FIXTURE_DIR "/gpt.img.gz")});
        QVERIFY(gzip.waitForFinished(10000));
        QCOMPARE(gzip.exitCode(), 0);
        fixture_ = gzip.readAllStandardOutput();
        QVERIFY(!fixture_.isEmpty());
#endif
    }
    void cleanupTestCase() {
        if (previousSettings_.isNull())
            qunsetenv("SEVENZIP_SETTINGS_PATH");
        else
            qputenv("SEVENZIP_SETTINGS_PATH", previousSettings_);
    }
    void cleanup() {
        QtLoadTranslation("-");
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void geometryAndPartitionSelection_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {"en", "ru"}) {
            QTest::newRow(qPrintable(language + "-normal")) << language << QSize(620, 390);
            QTest::newRow(qPrintable(language + "-compact")) << language << QSize(420, 320);
        }
    }
    void geometryAndPartitionSelection() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QtLoadTranslation(language == "en" ? "-" : language);
        auto bytes = std::make_shared<CDeviceBytes>(fixture_);
        auto services = Services(bytes);
        const auto open = services.open;
        const QString denied = QCoreApplication::translate(
            "deviceaccess", "macOS denied device access after administrator authorization. "
                            "Enable Full Disk Access for Sevenzip in System Settings > "
                            "Privacy & Security, then restart Sevenzip and try again.");
        std::atomic<unsigned> attempts{0};
        services.open = [&](const ExternalDevice &device, const auto &cancelled) {
            if (!attempts++)
                return DeviceAccessResult{DeviceAccessStatus::Failed, denied, {}};
            return open(device, cancelled);
        };
        DeviceSelectionDialog dialog(catalog_, nullptr, services);
        dialog.resize(size);
        dialog.show();
        QTRY_VERIFY(Items(dialog)->isEnabled());
        QCOMPARE(Items(dialog)->topLevelItemCount(), 2);
        QVERIFY(Items(dialog)->topLevelItem(0)->isDisabled());
        QVERIFY(!Open(dialog)->isEnabled());
        Items(dialog)->setCurrentItem(Items(dialog)->topLevelItem(1));
        QVERIFY(Open(dialog)->isEnabled());
        auto inspect = [&](const QString &state) {
            dialog.layout()->activate();
            QCoreApplication::processEvents();
            auto *buttons = dialog.findChild<QDialogButtonBox *>();
            QVERIFY(UiInspection::fullyVisible(*Items(dialog), dialog));
            QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
            QVERIFY(UiInspection::separate(*Items(dialog), *buttons, dialog));
            for (auto *button : buttons->findChildren<QPushButton *>())
                if (button->isVisible()) {
                    QVERIFY(UiInspection::fullyVisible(*button, dialog));
                    QVERIFY(button->width() >= button->sizeHint().width());
                }
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->isVisible()) {
                    QVERIFY(UiInspection::fullyVisible(*label, dialog));
                    QVERIFY2(UiInspection::labelFits(*label), qPrintable(label->text()));
                    QVERIFY(UiInspection::separate(*label, *Items(dialog), dialog));
                    QVERIFY(UiInspection::separate(*label, *buttons, dialog));
                }
            dialog.setProperty("uiInspection_language", language);
            dialog.setProperty("uiInspection_state", state);
            QVERIFY(UiInspection::writeReport(
                dialog, "devices-" + language + '-' + QString::number(size.width()) + '-' + state));
        };
        inspect("devices");
        Open(dialog)->click();
        QTRY_VERIFY(Open(dialog)->isEnabled());
        QCOMPARE(dialog.findChild<QLabel *>("deviceMessage")->text(), denied);
        QVERIFY(!dialog.selectedStream());
        inspect("denied");
        Open(dialog)->click();
        auto *back = dialog.findChild<QPushButton *>("deviceBackButton");
        QTRY_VERIFY(back->isVisible() && back->isEnabled());
        QCOMPARE(Items(dialog)->topLevelItemCount(), 2);
        inspect("partitions");
        back->click();
        QVERIFY(!back->isVisible());
        Items(dialog)->setCurrentItem(Items(dialog)->topLevelItem(1));
        Open(dialog)->click();
        QTRY_VERIFY(back->isVisible() && back->isEnabled());
        Items(dialog)->setCurrentItem(Items(dialog)->topLevelItem(0));
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QTest::keyClick(Items(dialog), Qt::Key_Return);
        QCOMPARE(accepted.size(), 1);
        QVERIFY(dynamic_cast<DirectDeviceInput *>(dialog.selectedStream()));
    }
    void emptyListRefreshAndDeniedAuthorizationAllowRetry() {
        auto services = Services(std::make_shared<CDeviceBytes>(fixture_));
        auto enumerate = services.enumerate;
        auto open = services.open;
        std::atomic<unsigned> enumerations{0}, attempts{0};
        services.enumerate = [&](QString &error) {
            return enumerations++ ? enumerate(error) : QList<ExternalDevice>{};
        };
        services.open = [&](const ExternalDevice &device, const auto &cancelled) {
            if (!attempts++)
                return DeviceAccessResult{DeviceAccessStatus::PermissionRequired, {}, {}};
            return open(device, cancelled);
        };
        DeviceSelectionDialog dialog(catalog_, nullptr, services);
        dialog.show();
        QTRY_VERIFY(Items(dialog)->isEnabled());
        QCOMPARE(Items(dialog)->topLevelItemCount(), 0);
        QVERIFY(!Open(dialog)->isEnabled());
        dialog.findChild<QPushButton *>("deviceRefreshButton")->click();
        QTRY_VERIFY(Items(dialog)->isEnabled());
        QCOMPARE(Items(dialog)->topLevelItemCount(), 2);
        Items(dialog)->setCurrentItem(Items(dialog)->topLevelItem(1));
        Open(dialog)->click();
        QTRY_VERIFY(Open(dialog)->isEnabled());
        QVERIFY(!dialog.selectedStream());
        QVERIFY(!dialog.findChild<QLabel *>("deviceMessage")->text().isEmpty());
        Open(dialog)->click();
        QTRY_VERIFY(dialog.findChild<QPushButton *>("deviceBackButton")->isVisible());
        dialog.reject();
        QVERIFY(!dialog.selectedStream());
    }
    void backReleasesSourceBeforeAnotherExclusiveOpen() {
        auto services = Services(std::make_shared<CDeviceBytes>(fixture_));
        std::weak_ptr<DeviceReadHandle> active;
        services.open = [&](const ExternalDevice &, const auto &) {
            if (!active.expired())
                return DeviceAccessResult{DeviceAccessStatus::Busy, {}, {}};
            auto handle = std::make_shared<CDeviceBytes>(fixture_);
            active = handle;
            return DeviceAccessResult{DeviceAccessStatus::Ready, {}, handle};
        };
        DeviceSelectionDialog dialog(catalog_, nullptr, services);
        dialog.show();
        QTRY_VERIFY(Items(dialog)->isEnabled());
        auto *back = dialog.findChild<QPushButton *>("deviceBackButton");
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            Items(dialog)->setCurrentItem(Items(dialog)->topLevelItem(1));
            Open(dialog)->click();
            QTRY_VERIFY(back->isVisible() && back->isEnabled());
            QVERIFY(!active.expired());
            back->click();
            QVERIFY(active.expired());
        }
        dialog.reject();
    }
    void pendingAuthorizationCanBeCanceledWithoutPublishingStream_data() {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("reject") << false;
        QTest::newRow("destroy") << true;
    }
    void pendingAuthorizationCanBeCanceledWithoutPublishingStream() {
        QFETCH(bool, destroy);
        std::atomic<bool> entered{false};
        std::atomic<bool> completed{false};
        auto services = Services(std::make_shared<CDeviceBytes>(fixture_));
        services.open = [&](const ExternalDevice &, const auto &cancelled) {
            entered = true;
            while (!cancelled())
                QThread::msleep(1);
            completed = true;
            return DeviceAccessResult{DeviceAccessStatus::Cancelled, {}, {}};
        };
        auto dialog = std::make_unique<DeviceSelectionDialog>(catalog_, nullptr, services);
        dialog->show();
        QTRY_VERIFY(Items(*dialog)->isEnabled());
        Items(*dialog)->setCurrentItem(Items(*dialog)->topLevelItem(1));
        Open(*dialog)->click();
        QTRY_VERIFY(entered.load());
        QVERIFY(!completed.load());
        if (destroy)
            dialog.reset();
        else {
            QSignalSpy rejected(dialog.get(), &QDialog::rejected);
            QTest::keyClick(dialog.get(), Qt::Key_Escape);
            QTRY_COMPARE(rejected.size(), 1);
            QVERIFY(!dialog->selectedStream());
        }
        QVERIFY(completed.load());
    }
    void pickerKeepsInitiatingPanel_data() {
        QTest::addColumn<unsigned>("owner");
        QTest::addColumn<bool>("accept");
        for (unsigned owner : {0u, 1u})
            for (bool accept : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(owner).arg(accept)))
                    << owner << accept;
    }
    void pickerKeepsInitiatingPanel() {
        QFETCH(unsigned, owner);
        QFETCH(bool, accept);
        auto bytes = std::make_shared<CDeviceBytes>(fixture_);
        const QByteArray original = bytes->Bytes;
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MainWindow window(&backend, {}, nullptr, nullptr, Services(bytes));
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.findChild<QAction *>("twoPanelsAction")->trigger();
        FileTable *views[2];
        QLineEdit *addresses[2];
        QString prefixes[2];
        for (unsigned index : {0u, 1u}) {
            const QString path = directory.filePath(QString::number(index));
            QVERIFY(QDir().mkpath(path));
            QFile file(QDir(path).filePath("panel.txt"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("panel payload"), qint64(13));
            file.close();
            auto *pane = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
            views[index] = pane->findChild<FileTable *>("archiveTable");
            addresses[index] = pane->findChild<QLineEdit *>("archivePath");
            addresses[index]->setFocus();
            QTRY_VERIFY(addresses[index]->hasFocus());
            window.openPath(path);
            QTRY_VERIFY(Row(*views[index], "panel.txt") >= 0);
            prefixes[index] = ToQString(views[index]->sourcePanel->GetCurrentFolderPrefix());
        }
        const unsigned peer = 1 - owner;
        views[peer]->selectRow(Row(*views[peer], "panel.txt"));
        const QString peerDraft = QStringLiteral("peer черновик 空");
        addresses[peer]->setText(peerDraft);
        addresses[owner]->setFocus();
        QTRY_VERIFY(addresses[owner]->hasFocus());
        bool closed = false;
        bool openingError = false;
        unsigned stage = 0;
        QTimer automation;
        connect(&automation, &QTimer::timeout, &window, [&] {
            if (auto *message = window.findChild<QMessageBox *>()) {
                openingError = true;
                message->accept();
                return;
            }
            auto *dialog = window.findChild<DeviceSelectionDialog *>();
            if (!dialog || !Items(*dialog)->isEnabled())
                return;
            if (stage == 0 && Items(*dialog)->topLevelItemCount() == 2) {
                connect(dialog, &QDialog::finished, &window, [&] {
                    window.activateWindow();
                    addresses[peer]->setFocus();
                    QTRY_VERIFY(addresses[peer]->hasFocus());
                    closed = true;
                });
                if (!accept) {
                    automation.stop();
                    dialog->reject();
                    return;
                }
                Items(*dialog)->setCurrentItem(Items(*dialog)->topLevelItem(1));
                ++stage;
                Open(*dialog)->click();
            } else if (stage == 1 &&
                       dialog->findChild<QPushButton *>("deviceBackButton")->isVisible()) {
                Items(*dialog)->setCurrentItem(Items(*dialog)->topLevelItem(0));
                ++stage;
                Open(*dialog)->click();
            }
        });
        automation.start(10);
        auto *open = window.findChild<QAction *>("openDeviceAction");
        QTRY_VERIFY(open->isEnabled());
        open->trigger();
        automation.stop();
        QVERIFY(closed);
        QVERIFY(!openingError);
        if (accept)
            QTRY_VERIFY(Row(*views[owner], QStringLiteral("папка 空")) >= 0);
        else {
            QCOMPARE(ToQString(views[owner]->sourcePanel->GetCurrentFolderPrefix()),
                     prefixes[owner]);
            QVERIFY(Row(*views[owner], "panel.txt") >= 0);
        }
        QCOMPARE(ToQString(views[peer]->sourcePanel->GetCurrentFolderPrefix()), prefixes[peer]);
        QCOMPARE(addresses[peer]->text(), peerDraft);
        QVERIFY(views[peer]->selectionModel()->isRowSelected(Row(*views[peer], "panel.txt"), {}));
        QCOMPARE(bytes->Bytes, original);
    }
    void menuToSourcePanelCopyRefreshAndParent_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("needsRecovery");
        QTest::addColumn<bool>("acceptRecovery");
        QTest::addColumn<QString>("copyCommand");
        QTest::addColumn<bool>("nestedArchive");
        QTest::addColumn<int>("readonlyBit");
        QTest::addColumn<bool>("exfat");
        for (const QString &language : {"en", "ru"}) {
            QTest::newRow(qPrintable(language))
                << language << false << false << QString() << false << -1 << false;
            QTest::newRow(qPrintable(language + "-cancel-recovery"))
                << language << true << false << QString() << false << -1 << false;
            QTest::newRow(qPrintable(language + "-accept-recovery"))
                << language << true << true << QString() << false << -1 << false;
            for (bool accept : {false, true})
                QTest::newRow(qPrintable(language + (accept ? "-accept-orphan" : "-cancel-orphan")))
                    << language << true << accept << QString() << false << 16 << false;
            for (const QString &command : {"copyAction", "extractArchiveAction"}) {
                const bool nestedArchive = language == "en";
                QTest::newRow(qPrintable(language + '-' + command +
                                         (nestedArchive ? "-nested" : "-timestamp")))
                    << language << true << true << command << nestedArchive << -1 << false;
            }
        }
        QTest::newRow("exfat-copy-nested")
            << QString("en") << true << true << QString("copyAction") << true << -1 << true;
        QTest::newRow("exfat-extract-nested")
            << QString("ru") << true << true << QString("extractArchiveAction") << true << -1
            << true;
        QTest::newRow("exfat-cancel")
            << QString("en") << true << false << QString() << false << -1 << true;
    }
    void menuToSourcePanelCopyRefreshAndParent() {
        QFETCH(QString, language);
        QFETCH(bool, needsRecovery);
        QFETCH(bool, acceptRecovery);
        QFETCH(QString, copyCommand);
        QFETCH(bool, nestedArchive);
        QFETCH(int, readonlyBit);
        QFETCH(bool, exfat);
        SaveRegLang(UString(language == "en" ? L"-" : L"ru"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        auto bytes = std::make_shared<CDeviceBytes>(fixture_);
        if (exfat) {
            QTemporaryDir sample;
            QVERIFY(sample.isValid());
            const QString gzipPath = sample.filePath("payload.gz");
            QProcess gzip;
            gzip.start(engine_, {"a", "-tgzip", "-si", "--", gzipPath});
            QVERIFY(gzip.waitForStarted());
            QCOMPARE(gzip.write("SELECT 1;\n"), qint64(10));
            gzip.closeWriteChannel();
            QVERIFY(gzip.waitForFinished(10000));
            QCOMPARE(gzip.exitCode(), 0);
            QFile packed(gzipPath);
            QVERIFY(packed.open(QIODevice::ReadOnly));
            ExfatFixture fixture;
            fixture.setNestedFile(QStringLiteral("данные с пробелом.sql.gz"), packed.readAll());
            fixture.image[106] = char(2);
            bytes->Bytes = fixture.image;
        } else if (!copyCommand.isEmpty()) {
            QProcess gzip;
            gzip.start(engine_, {"x", "-so", "--",
                                 QStringLiteral(DEVICE_FIXTURE_DIR "/ext4-timestamp.img.gz")});
            QVERIFY(gzip.waitForFinished(10000));
            QCOMPARE(gzip.exitCode(), 0);
            bytes->Bytes = gzip.readAllStandardOutput();
            QVERIFY(!bytes->Bytes.isEmpty());
        }
        if (needsRecovery && !exfat) {
            // Controlled Ext declarations inside the first GPT partition.
            const int superblock = copyCommand.isEmpty() ? 1024 * 1024 + 1024 : 1024;
            bytes->Bytes[superblock + 0x65] = char(quint8(bytes->Bytes[superblock + 0x65]) & ~4u);
            if (readonlyBit < 0)
                bytes->Bytes[superblock + 0x60] =
                    char(quint8(bytes->Bytes[superblock + 0x60]) | 4u);
            else {
                bytes->Bytes[superblock + 0x60] =
                    char(quint8(bytes->Bytes[superblock + 0x60]) & ~4u);
                const int field = superblock + 0x64 + readonlyBit / 8;
                bytes->Bytes[field] = char(quint8(bytes->Bytes[field]) | (1u << (readonlyBit % 8)));
            }
        }
        const QByteArray originalBytes = bytes->Bytes;
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MainWindow window(&backend, {}, nullptr, nullptr, Services(bytes));
        window.openPath(directory.path());
        window.show();
        auto *action = window.findChild<QAction *>("openDeviceAction");
        QVERIFY(action);
        QTRY_VERIFY(action->isEnabled());
        QTimer automation;
        unsigned stage = 0;
        QString openingError;
        QString openingLimitations;
        bool errorGeometry = false;
        connect(&automation, &QTimer::timeout, &window, [&] {
            if (needsRecovery && stage == 2) {
                if (auto *message = window.findChild<QMessageBox *>()) {
                    openingError = message->text();
                    openingLimitations = message->informativeText();
                    message->layout()->activate();
                    errorGeometry = true;
                    for (auto *label : message->findChildren<QLabel *>())
                        if (label->isVisible())
                            errorGeometry &= UiInspection::fullyVisible(*label, *message) &&
                                             UiInspection::labelFits(*label);
                    message->setProperty("uiInspection_language", language);
                    message->setProperty("uiInspection_state", "limited-read");
                    errorGeometry &= UiInspection::writeReport(
                        *message, "device-limitations-" + (exfat ? QString("exfat-") : QString()) +
                                      language + (readonlyBit < 0 ? "-journal" : "-orphan"));
                    ++stage;
                    automation.stop();
                    if (acceptRecovery)
                        message->findChild<QPushButton *>("deviceOpenWithoutRecoveryButton")
                            ->click();
                    else
                        QTest::keyClick(message,
                                        language == "en" ? Qt::Key_Return : Qt::Key_Escape);
                }
                return;
            }
            auto *dialog = window.findChild<DeviceSelectionDialog *>();
            if (!dialog || !Items(*dialog)->isEnabled())
                return;
            auto *back = dialog->findChild<QPushButton *>("deviceBackButton");
            if (stage == 0 && Items(*dialog)->topLevelItemCount() == 2) {
                Items(*dialog)->setCurrentItem(Items(*dialog)->topLevelItem(1));
                stage = !exfat && copyCommand.isEmpty() ? 1u : 2u;
                Open(*dialog)->click();
            } else if (stage == 1 && back->isVisible()) {
                Items(*dialog)->setCurrentItem(Items(*dialog)->topLevelItem(0));
                ++stage;
                if (!needsRecovery)
                    automation.stop();
                Open(*dialog)->click();
            }
        });
        automation.start(10);
        action->trigger();
        if (needsRecovery) {
            QCOMPARE(stage, 3u);
            QVERIFY(!openingError.isEmpty());
            if (exfat)
                QVERIFY(!openingLimitations.isEmpty());
            else
                QVERIFY(
                    openingLimitations.contains(readonlyBit < 0 ? "RECOVER" : "ORPHAN_PRESENT"));
            QVERIFY(errorGeometry);
            if (!acceptRecovery) {
                QVERIFY(!window.findChild<QLabel *>("deviceReadOnlyLabel")->isVisible());
                QVERIFY(action->isEnabled());
                QCOMPARE(bytes->Bytes, originalBytes);
                return;
            }
        } else
            QCOMPARE(stage, 2u);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        const QString folder = exfat || copyCommand.isEmpty()
                                   ? QStringLiteral("папка 空")
                                   : QStringLiteral(".internal/twiMonBot");
        const QStringList folders = folder.split('/');
        QTRY_VERIFY(Row(*table, folders.first()) >= 0);
        auto *badge = window.findChild<QLabel *>("deviceReadOnlyLabel");
        QVERIFY(badge && badge->isVisible());
        const QString badgeText = badge->text();
        if (needsRecovery)
            QCOMPARE(badgeText,
                     QCoreApplication::translate("mainwindow_panel", "Read only · limited access"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            window.layout()->activate();
            QCoreApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*badge, window));
            QVERIFY(UiInspection::labelFits(*badge));
            window.setProperty("uiInspection_language", language);
            window.setProperty("uiInspection_state",
                               needsRecovery ? "journal-not-replayed" : "direct-device");
            QVERIFY(UiInspection::writeReport(
                window, (needsRecovery ? "device-journal-panel-" : "device-panel-") +
                            (exfat ? QString("exfat-") : QString()) + language + '-' +
                            QString::number(size.width())));
        }
        for (const QString &part : folders) {
            QTRY_VERIFY(Row(*table, part) >= 0);
            QVERIFY(QMetaObject::invokeMethod(
                table, "doubleClicked", Qt::DirectConnection,
                Q_ARG(QModelIndex, table->model()->index(Row(*table, part), 0))));
        }
        QString name = exfat                   ? QStringLiteral("данные с пробелом.sql.gz")
                       : copyCommand.isEmpty() ? QStringLiteral("данные с пробелом.txt")
                                               : QStringLiteral("2024-05-07T02:00:02Z.sql.gz");
        const QString outerName = name;
        if (nestedArchive) {
            QTRY_VERIFY(Row(*table, name) >= 0);
            QVERIFY(QMetaObject::invokeMethod(
                table, "doubleClicked", Qt::DirectConnection,
                Q_ARG(QModelIndex, table->model()->index(Row(*table, name), 0))));
            name.chop(3);
#ifdef Q_OS_WIN
            // The nameless gzip derives its name from the platform-corrected
            // temporary file used when entering an archive from a device.
            name.replace(':', '_');
#endif
        }
#ifdef Q_OS_WIN
        const QString outputName = QString(name).replace(':', '_');
#else
        const QString outputName = name;
#endif
        QTRY_VERIFY(Row(*table, name) >= 0);
        table->selectRow(Row(*table, name));
        table->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(window.findChild<QAction *>("copyAction")->isEnabled());
        for (const char *command : {"deleteAction", "renameAction", "moveAction"})
            QVERIFY(!window.findChild<QAction *>(command)->isEnabled());
        const QString output = directory.filePath("copied");
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        if (copyCommand.isEmpty())
            window.extractTo(output, true, {}, options);
        else {
            QVERIFY(QDir().mkpath(output));
            bool dialogSeen = false;
            QTimer::singleShot(0, &window, [&] {
                if (auto *dialog = window.findChild<CopyDialog *>("copyDialog")) {
                    dialogSeen = true;
                    dialog->findChild<QComboBox *>("copyDestination")->setCurrentText(output + '/');
                    dialog->accept();
                }
            });
            window.findChild<QAction *>(qPrintable(copyCommand))->trigger();
            QVERIFY(dialogSeen);
        }
        const QString copiedPath =
            output + '/' + (copyCommand.isEmpty() ? folder + '/' : QString()) + outputName;
        QTRY_VERIFY(QFileInfo::exists(copiedPath));
        QFile copied(copiedPath);
        QVERIFY2(copied.open(QIODevice::ReadOnly), qPrintable(copied.fileName()));
        const QByteArray copiedBytes = copied.readAll();
        if (copyCommand.isEmpty())
            QCOMPARE(copiedBytes, QByteArray("device ext4 payload\n"));
        else if (nestedArchive)
            QCOMPARE(copiedBytes, QByteArray("SELECT 1;\n"));
        else {
            QVERIFY(!copiedBytes.isEmpty());
            QProcess gzip;
            gzip.start(engine_, {"x", "-so", "--", copiedPath});
            QVERIFY(gzip.waitForFinished(10000));
            QCOMPARE(gzip.exitCode(), 0);
            QCOMPARE(gzip.readAllStandardOutput(), QByteArray("SELECT 1;\n"));
        }
        window.findChild<QAction *>("refreshAction")->trigger();
        QTRY_VERIFY(Row(*table, name) >= 0);
        QCOMPARE(badge->text(), badgeText);
        if (nestedArchive) {
            const QString allOutput = directory.filePath("whole-gzip");
            window.extractTo(allOutput, false, {}, options);
            QFile allCopied(allOutput + '/' + outputName);
            QVERIFY(allCopied.open(QIODevice::ReadOnly));
            QCOMPARE(allCopied.readAll(), copiedBytes);
            window.findChild<QAction *>("parentFolderAction")->trigger();
            QTRY_VERIFY(Row(*table, outerName) >= 0);
        }
        for (int index = folders.size() - 1; index >= 0; --index) {
            window.findChild<QAction *>("parentFolderAction")->trigger();
            QTRY_VERIFY(Row(*table, folders[index]) >= 0);
        }
        const QString allOutput = directory.filePath("whole-filesystem");
        window.extractTo(allOutput, false, {}, options);
        QFile allCopied(allOutput + '/' + folder + '/' + outputName + (nestedArchive ? ".gz" : ""));
        QVERIFY(allCopied.open(QIODevice::ReadOnly));
        if (!nestedArchive)
            QCOMPARE(allCopied.readAll(), copiedBytes);
        window.findChild<QAction *>("parentFolderAction")->trigger();
        QTRY_VERIFY(!badge->isVisible());
        QVERIFY(QFileInfo::exists(copied.fileName()));
        QCOMPARE(bytes->Bytes, originalBytes);
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(DeviceUiTests)
#include "device_ui_tests.moc"
