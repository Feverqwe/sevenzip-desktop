#include "archive/backend/archivebackend.h"
#include "archive/support/source_extract_fixture.h"
#include "filemanager/qtfilemanagerstartup.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "platform/associations/qtassociationfileopen.h"
#include "support/slow_codec.h"
#include "support/widget_dialogs.h"
#include <QFileOpenEvent>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {
class FirstDocumentContents final : public QObject {
    QSet<MainWindow *> shown_;

  public:
    QList<QStringList> contents;
    bool eventFilter(QObject *object, QEvent *event) override {
        auto *window = dynamic_cast<MainWindow *>(object);
        if (!window || event->type() != QEvent::Show || shown_.contains(window))
            return false;
        shown_.insert(window);
        QStringList names;
        if (auto *table = window->findChild<FileTable *>("archiveTable"))
            for (int row = 0; row < table->rowCount(); ++row)
                names.append(table->model()->index(row, 0).data().toString());
        contents.append(names);
        return false;
    }
};
} // namespace

class AssociatedWindowTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QList<QPointer<MainWindow>> windows_;

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void cleanup() {
        for (auto &window : windows_)
            delete window.data();
        windows_.clear();
    }
    void earlyAndRepeatedRequestsOpenRealArchivesAfterDiscovery() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto closeWindows = qScopeGuard([this] { cleanup(); });
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        const QByteArray payload("associated file bytes\0\xff", 23);
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), payload));
        const QString archive = fixture.filePath(QStringLiteral("архив 空 with spaces.zip"));
        QProcess engine;
        engine.setWorkingDirectory(fixture.path());
        engine.start(QStandardPaths::findExecutable("7z"), {"a", "-tzip", archive, name});
        QVERIFY(engine.waitForFinished(10000));
        QCOMPARE(engine.exitCode(), 0);
        const auto original = SourceExtractFixture::readFile(archive);
        FirstDocumentContents firstShow;
        qApp->installEventFilter(&firstShow);
        QStringList errors;
        QObject responder;
        dialogs_.respond(responder, [&](QDialog &dialog) {
            if (auto *message = qobject_cast<QMessageBox *>(&dialog)) {
                errors.append(message->text());
                message->accept();
            }
        });
        QObject source;
        QtAssociationFileOpen events(source);
        connect(&events, &QtAssociationFileOpen::OpenRequested, this,
                [&](const QString &path) { windows_.append(OpenSevenzipAssociatedFile(path)); });
        QFileOpenEvent early(archive);
        QVERIFY(QApplication::sendEvent(&source, &early));
        events.Activate();
        QTRY_COMPARE(windows_.size(), 1);
        QTRY_VERIFY(!windows_[0].isNull());
        auto *first = windows_[0]->findChild<FileTable *>("archiveTable");
        QVERIFY(first);
        QTRY_VERIFY(first->rowCount() == 1 &&
                    first->model()->index(0, 0).data().toString() == name);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(firstShow.contents, QList<QStringList>{QStringList{name}});

        QFileOpenEvent repeated(archive);
        QVERIFY(QApplication::sendEvent(&source, &repeated));
        QTRY_COMPARE(windows_.size(), 2);
        QVERIFY(windows_[0] != windows_[1]);
        auto *second = windows_[1]->findChild<FileTable *>("archiveTable");
        QVERIFY(second);
        QTRY_VERIFY(second->rowCount() == 1 &&
                    second->model()->index(0, 0).data().toString() == name);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(firstShow.contents, (QList<QStringList>{QStringList{name}, QStringList{name}}));
        QCOMPARE(first->model()->index(0, 0).data().toString(), name);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(name)), payload);
        QVERIFY(windows_[1]->close());
        QTRY_VERIFY(windows_[1].isNull());
        QCOMPARE(first->model()->index(0, 0).data().toString(), name);
    }
    void closeDuringDiscoveryJoinsAndDoesNotOpenAnArchive() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        SlowCodecModuleFixture slow(fixture, 300);
        QVERIFY(slow.Valid);
        const QByteArray previous = qgetenv("SEVENZIP_CODEC_MODULE_PATH");
        const bool wasSet = qEnvironmentVariableIsSet("SEVENZIP_CODEC_MODULE_PATH");
        const auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv("SEVENZIP_CODEC_MODULE_PATH", previous);
            else
                qunsetenv("SEVENZIP_CODEC_MODULE_PATH");
        });
        qputenv("SEVENZIP_CODEC_MODULE_PATH", slow.Path.toUtf8());
        QPointer<MainWindow> window =
            OpenSevenzipAssociatedFile(fixture.filePath("not-opened.zip"));
        windows_.append(window);
        auto *backend = window->findChild<ArchiveBackend *>();
        QVERIFY(backend);
        QVERIFY(backend->busy());
        QVERIFY(!window->isVisible());
        QTRY_VERIFY(QFileInfo::exists(slow.Entered));
        QSignalSpy completed(backend, &ArchiveBackend::finished);
        QVERIFY(window->close());
        QTRY_VERIFY(window.isNull());
        QVERIFY(QFileInfo::exists(slow.Finished));
        QCOMPARE(completed.count(), 0);
        QVERIFY(!QFileInfo::exists(fixture.filePath("not-opened.zip")));
    }
    void missingDocumentReportsItsPathAndAllowsAnotherOpen() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto closeWindows = qScopeGuard([this] { cleanup(); });
        const QString missing = fixture.filePath("missing & archive.zip");
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), "next archive");
        QVERIFY(!archive.isEmpty());
        QString message;
        QObject responder;
        dialogs_.respond(responder, [&](QDialog &dialog) {
            if (auto *warning = qobject_cast<QMessageBox *>(&dialog)) {
                message = warning->text();
                warning->accept();
            }
        });
        windows_.append(OpenSevenzipAssociatedFile(missing));
        QTRY_VERIFY(message.contains(missing));
        QVERIFY(windows_[0] && windows_[0]->isVisible());
        auto *address = windows_[0]->findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        QCOMPARE(QDir::cleanPath(address->text()), QDir::cleanPath(fixture.path()));

        windows_.append(OpenSevenzipAssociatedFile(archive));
        auto *table = windows_[1]->findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QTRY_VERIFY(windows_[1]->isVisible() && table->rowCount() == 1 &&
                    table->model()->index(0, 0).data().toString() == "inside");
    }
    void discoveryFailureReportsItsErrorWithoutStartingArchiveRead() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const auto closeWindows = qScopeGuard([this] { cleanup(); });
        const QByteArray previous = qgetenv("SEVENZIP_CODEC_MODULE_PATH");
        const bool wasSet = qEnvironmentVariableIsSet("SEVENZIP_CODEC_MODULE_PATH");
        const auto restore = qScopeGuard([&] {
            if (wasSet)
                qputenv("SEVENZIP_CODEC_MODULE_PATH", previous);
            else
                qunsetenv("SEVENZIP_CODEC_MODULE_PATH");
        });
        qputenv("SEVENZIP_CODEC_MODULE_PATH", fixture.filePath("missing-engine").toUtf8());
        QString message;
        QObject responder;
        dialogs_.respond(responder, [&](QDialog &dialog) {
            if (auto *error = qobject_cast<QMessageBox *>(&dialog)) {
                message = error->text();
                QCOMPARE(error->icon(), QMessageBox::Critical);
                error->accept();
            }
        });
        auto *window = OpenSevenzipAssociatedFile(fixture.filePath("not-opened.zip"));
        windows_.append(window);
        auto *backend = window->findChild<ArchiveBackend *>();
        QVERIFY(backend);
        QSignalSpy readCompleted(backend, &ArchiveBackend::finished);
        QTRY_VERIFY(!message.isEmpty());
        QVERIFY(window->isVisible());
        QVERIFY(!backend->busy());
        QCOMPARE(readCompleted.count(), 0);
    }
#ifdef Q_OS_MACOS
    void initialDocumentLaunchOmitsDirectoryWindowAndRetainsLaterDocuments() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), "launch bytes");
        QVERIFY(!archive.isEmpty());
        const auto original = SourceExtractFixture::readFile(archive);
        const auto closeWindows = qScopeGuard([&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *window = dynamic_cast<MainWindow *>(widget))
                    delete window;
        });
        QCoreApplication::postEvent(qApp, new QFileOpenEvent(archive));
        int initialCount = -1, finalCount = -1;
        bool timedOut = false, firstRetained = false;
        QPointer<MainWindow> first;
        QTimer observer, watchdog;
        QObject responder;
        QString error;
        dialogs_.respond(responder, [&](QDialog &dialog) {
            if (auto *message = qobject_cast<QMessageBox *>(&dialog)) {
                error = message->text();
                message->accept();
                qApp->quit();
            }
        });
        connect(&observer, &QTimer::timeout, this, [&] {
            QList<MainWindow *> visible;
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *window = dynamic_cast<MainWindow *>(widget);
                    window && window->isVisible())
                    visible.append(window);
            int archives = 0;
            for (auto *window : visible) {
                auto *table = window->findChild<FileTable *>("archiveTable");
                if (table && table->rowCount() == 1 &&
                    table->model()->index(0, 0).data().toString() == "inside") {
                    ++archives;
                    if (!first)
                        first = window;
                }
            }
            if (archives == 1 && initialCount == -1) {
                initialCount = visible.size();
                QCoreApplication::postEvent(qApp, new QFileOpenEvent(archive));
            } else if (archives == 2) {
                finalCount = visible.size();
                firstRetained = first && visible.contains(first.data());
                observer.stop();
                qApp->quit();
            }
        });
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, this, [&] {
            timedOut = true;
            qApp->quit();
        });
        observer.start(10);
        watchdog.start(10000);
        QCOMPARE(RunSevenzipDesktop(*qApp), 0);
        observer.stop();
        watchdog.stop();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!timedOut);
        QCOMPARE(initialCount, 1);
        QCOMPARE(finalCount, 2);
        QVERIFY(firstRetained);
        QCOMPARE(SourceExtractFixture::readFile(archive), original);
    }
#endif
};
QTEST_MAIN(AssociatedWindowTests)
#include "associated_window_tests.moc"
