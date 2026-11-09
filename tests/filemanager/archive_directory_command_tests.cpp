#include "7zip/PropID.h"
#include "archive/backend/archivebackend.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "localization/qtlanguageservices.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "ui/progress/ProgressDialog2.h"
#include <QAction>
#include <QFile>
#include <QLineEdit>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>
#include <utility>

namespace {
class OnProgressShow final : public QObject {
    const std::function<void(CProgressDialog &)> shown_;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show)
            if (auto *dialog = dynamic_cast<CProgressDialog *>(object))
                shown_(*dialog);
        return false;
    }

  public:
    explicit OnProgressShow(std::function<void(CProgressDialog &)> shown)
        : shown_(std::move(shown)) {
        qApp->installEventFilter(this);
    }
    ~OnProgressShow() override { qApp->removeEventFilter(this); }
};
int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
} // namespace

class ArchiveDirectoryCommandTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official codec module unavailable; directory commands unverified");
#else
        module_ = QStringLiteral(SEVENZIP_CODEC_MODULE);
        engine_ = QFileInfo(module_).dir().filePath(
#ifdef Q_OS_WIN
            "7z.exe"
#else
            "7z"
#endif
        );
#endif
    }
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void completeArchiveDoesNotOfferDirectoryRead_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("zip") << QString("zip");
        QTest::newRow("7z") << QString("7z");
    }
    void completeArchiveDoesNotOfferDirectoryRead() {
        QFETCH(QString, format);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString directory = QStringLiteral("implicit папка");
        const QString relative = directory + QStringLiteral("/данные 空.txt");
        QVERIFY(QDir(fixture.path()).mkpath(directory));
        QFile input(fixture.filePath(relative));
        const QByteArray payload("directory payload");
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        const QString path = fixture.filePath("archive." + format);
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine_, {"a", "-t" + format, "--", path, relative});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QByteArray before = archive.readAll();
        archive.close();
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY(ready[0][0].toBool());
        MainWindow window(&backend);
        window.show();
        window.openPath(path);
        auto *view = window.findChild<FileTable *>("archiveTable");
        auto *calculate = window.findChild<QAction *>("calculateDirectorySizeAction");
        bool reading = false;
        OnProgressShow observer([&](CProgressDialog &) { reading = true; });
        QTRY_VERIFY(view->isEnabled() && Row(*view, directory) >= 0);
        view->selectRow(Row(*view, directory));
        QVERIFY(!calculate->isEnabled());
        calculate->trigger();
        QVERIFY(!reading);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), before);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), payload);
    }
    void calculationKeepsSourcePanel_data() {
        QTest::addColumn<unsigned>("source");
        QTest::addColumn<bool>("cancel");
        for (unsigned source : {0u, 1u})
            for (bool cancel : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(source).arg(cancel)))
                    << source << cancel;
    }
    void calculationKeepsSourcePanel() {
        QFETCH(unsigned, source);
        QFETCH(bool, cancel);
        QFile compressed(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) + "/apfs-directories.img.z");
        QVERIFY(compressed.open(QIODevice::ReadOnly));
        const QByteArray image = qUncompress(compressed.readAll());
        QVERIFY(!image.isEmpty());
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QString::fromUtf8("каталоги 空.apfs")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(image), qint64(image.size()));
        input.close();
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        QVERIFY(ready[0][0].toBool());
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.findChild<QAction *>("twoPanelsAction")->trigger();
        const unsigned peer = 1 - source;
        auto *peerPane = window.findChild<QWidget *>(QString("filePanel%1").arg(peer));
        auto *peerView = peerPane->findChild<FileTable *>("archiveTable");
        auto *peerAddress = peerPane->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(peerAddress);
        QTRY_VERIFY(peerAddress->hasFocus());
        window.openPath(fixture.path());
        QTRY_VERIFY(Row(*peerView, QFileInfo(input).fileName()) >= 0);
        peerView->selectRow(Row(*peerView, QFileInfo(input).fileName()));
        const QString peerPrefix = ToQString(peerView->sourcePanel->GetCurrentFolderPrefix());
        const QString peerDraft = QStringLiteral("peer черновик 空");
        peerAddress->setText(peerDraft);
        auto *pane = window.findChild<QWidget *>(QString("filePanel%1").arg(source));
        auto *view = pane->findChild<FileTable *>("archiveTable");
        auto *address = pane->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        window.openPath(input.fileName());
        const QString directory = QStringLiteral("directory-00");
        QTRY_VERIFY(Row(*view, directory) >= 0);
        const QString sourcePrefix = ToQString(view->sourcePanel->GetCurrentFolderPrefix());
        view->selectRow(Row(*view, directory));
        const int column = view->columnForProperty(kpidSize);
        QVERIFY(column >= 0);
        const auto size = [&] {
            return view->model()->index(Row(*view, directory), column).data(Qt::UserRole + 2);
        };
        QVERIFY(!size().isValid());
        const QString draft = QStringLiteral("source черновик 空");
        address->setText(draft);
        auto *calculate = window.findChild<QAction *>("calculateDirectorySizeAction");
        QVERIFY(calculate && calculate->isEnabled());
        bool observed = false, closed = false;
        QTimer review;
        connect(&review, &QTimer::timeout, &window, [] {
            TestProgressDialog::DismissVisibleResult(QApplication::activeModalWidget());
        });
        OnProgressShow observer([&](CProgressDialog &dialog) {
            if (observed)
                return;
            observed = true;
            connect(&dialog, &QDialog::finished, &window, [&] {
                window.activateWindow();
                PanelKeys::focusAddress(peerAddress);
                closed = true;
            });
            if (cancel)
                dialog.Sync.Set_Stopped(true);
        });
        review.start(1);
        calculate->trigger();
        review.stop();
        QVERIFY(observed && closed);
        QCOMPARE(size().isValid(), !cancel);
        QCOMPARE(ToQString(view->sourcePanel->GetCurrentFolderPrefix()), sourcePrefix);
        QCOMPARE(address->text(), draft);
        QVERIFY(view->selectionModel()->isRowSelected(Row(*view, directory), {}));
        QCOMPARE(ToQString(peerView->sourcePanel->GetCurrentFolderPrefix()), peerPrefix);
        QCOMPARE(peerAddress->text(), peerDraft);
        QVERIFY(peerView->selectionModel()->isRowSelected(
            Row(*peerView, QFileInfo(input).fileName()), {}));
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        QCOMPARE(calculate->isEnabled(), cancel);
        if (cancel) {
            calculate->trigger();
            QVERIFY(size().isValid());
        }
        QVERIFY(size().toULongLong() > 0);
        QVERIFY(!view->model()
                     ->index(Row(*view, "directory-11"), column)
                     .data(Qt::UserRole + 2)
                     .isValid());
        QTRY_VERIFY(view->isEnabled() && Row(*view, directory) >= 0);
        view->selectRow(Row(*view, directory));
        QVERIFY(!calculate->isEnabled());
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), image);
    }
};

QTEST_MAIN(ArchiveDirectoryCommandTests)
#include "archive_directory_command_tests.moc"
