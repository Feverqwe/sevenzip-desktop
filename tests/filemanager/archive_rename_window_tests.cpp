#include "archive/agent/Agent.h"
#include "archive/support/native_archive_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "platform/support/external_editor_fixture.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QEventLoop>
#include <QMessageBox>
#include <QTimer>

class ArchiveRenameWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void archiveRenameUsesEditorText() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("source 文件.txt");
        const QString renamed = QString("renamed     ") + QChar(0x202E) + QStringLiteral("δ.txt");
        const QString zip = fixture.filePath(QStringLiteral("archive space.zip"));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(original), "control"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), zip, original));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openArchive(zip);
        auto *view = static_cast<FileTable *>(table(window));
        const auto currentEditor = [view]() -> QLineEdit * {
            for (auto *editor : view->findChildren<QLineEdit *>())
                if (editor->isVisible())
                    return editor;
            return nullptr;
        };
        QTRY_COMPARE(view->rowCount(), 1);
        view->selectRow(0);
        QVERIFY(action(window, "renameAction")->isEnabled());
        action(window, "renameAction")->trigger();
        auto *editor = currentEditor();
        QVERIFY(editor);
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_COMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), renamed);
        QCOMPARE(view->model()->index(0, 0).data().toString(),
                 QStringLiteral("renamed ... _δ.txt"));
        CMyComPtr<IFolderGetItemName> fast;
        CMyComPtr<IFolderFolder> folder = view->sourcePanel->GetFolder();
        QCOMPARE(folder.QueryInterface(IID_IFolderGetItemName, &fast), S_OK);
        const wchar_t *name = nullptr;
        unsigned length = 0;
        QCOMPARE(fast->GetItemName(0, &name, &length), S_OK);
        QCOMPARE(QString::fromWCharArray(name, int(length)), renamed);
        QCOMPARE(fast->GetItemSize(0), UInt64(7));
        QCOMPARE(ExternalEditorFixture::contents(zip, renamed), QByteArray("control"));
        view->selectRow(0);
        action(window, "renameAction")->trigger();
        editor = currentEditor();
        QVERIFY(editor);
        QCOMPARE(editor->text(), renamed);
        editor->setText("discarded.txt");
        QTest::keyClick(editor, Qt::Key_Escape);
        QApplication::processEvents();
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), renamed);
        QCOMPARE(ExternalEditorFixture::contents(zip, renamed), QByteArray("control"));
        QTimer closeError;
        QString message;
        QString errorObject;
        QEventLoop errorDelivery;
        connect(&closeError, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    message = box->text();
                    errorObject = box->objectName();
                    box->accept();
                    errorDelivery.quit();
                }
        });
        closeError.start(2);
        view->selectRow(0);
        action(window, "renameAction")->trigger();
        editor = currentEditor();
        QVERIFY(editor);
        QCOMPARE(editor->text(), renamed);
        editor->setText("..");
        QTest::keyClick(editor, Qt::Key_Return);
        QTimer::singleShot(5000, &errorDelivery, &QEventLoop::quit);
        if (message.isEmpty())
            errorDelivery.exec();
        QCOMPARE(errorObject, QString("panelArchiveError"));
        QCOMPARE(message, ToQString(HResultToMessage(E_INVALIDARG)));
        closeError.stop();
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), renamed);
        QCOMPARE(ExternalEditorFixture::contents(zip, renamed), QByteArray("control"));
    }

    void archiveRenameUsesSourceRelativePath_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("folder");
        for (const QString &format : {QString("zip"), QString("7z")})
            for (bool folder : {false, true})
                QTest::newRow(qPrintable(format + (folder ? "-folder" : "-file")))
                    << format << folder;
    }
    void archiveRenameUsesSourceRelativePath() {
        QFETCH(QString, format);
        QFETCH(bool, folder);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QVERIFY(!settings.isEmpty());
        QFile::remove(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original =
            folder ? QStringLiteral("source 文件/item.txt") : QStringLiteral("source 文件.txt");
        const QString directory = QStringLiteral("nested 空");
        const QString renamed = directory + QStringLiteral("/renamed δ.txt");
        const QString archive = fixture.filePath("archive space." + format);
        QVERIFY(QDir().mkpath(QFileInfo(fixture.filePath(original)).absolutePath()));
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(original), "control"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, original, format));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openArchive(archive);
        auto *view = static_cast<FileTable *>(table(window));
        QTRY_COMPARE(view->rowCount(), 1);
        CMyComPtr<IFolderFolder> provider = view->sourcePanel->GetFolder();
        const UString prefix = view->sourcePanel->GetCurrentFolderPrefix();
        view->selectRow(0);
        action(window, "renameAction")->trigger();
        auto *editor = view->findChild<QLineEdit *>();
        QVERIFY(editor);
        QString message;
        QEventLoop completion;
        QTimer responder, publication;
        connect(&responder, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    message = box->text();
                    box->accept();
                    completion.quit();
                }
        });
        connect(&publication, &QTimer::timeout, &window, [&] {
            if (view->rowCount() == 1 && view->model()->index(0, 0).isValid() &&
                view->model()->index(0, 0).data(Qt::UserRole + 3).toString() == directory)
                completion.quit();
        });
        responder.start(2);
        publication.start(5);
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTimer::singleShot(5000, &completion, &QEventLoop::quit);
        completion.exec();
        QVERIFY2(message.isEmpty(), qPrintable(message));
        QCOMPARE(view->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), directory);
        QCOMPARE(view->sourcePanel->GetFolder(), provider.Interface());
        QCOMPARE(view->sourcePanel->GetCurrentFolderPrefix(), prefix);
        QCOMPARE(ExternalEditorFixture::contents(archive, renamed + (folder ? "/item.txt" : "")),
                 QByteArray("control"));
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(original)), QByteArray("control"));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(window, QString("archive-rename-relative-%1-%2")
                                                          .arg(QTest::currentDataTag())
                                                          .arg(size.width())));
        }
    }

    void unsafeArchiveRenameKeepsOriginal_data() {
        QTest::addColumn<QString>("kind");
        for (const char *kind : {"parent", "absolute", "embedded-null", "empty"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void unsafeArchiveRenameKeepsOriginal() {
        QFETCH(QString, kind);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("source 文件.txt");
        const QString archive = fixture.filePath("protected archive.zip");
        const QString sentinel = fixture.filePath("outside.txt");
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(original), "control"));
        QVERIFY(ExternalEditorFixture::write(sentinel, "sentinel"));
        QVERIFY(ExternalEditorFixture::createArchive(fixture.path(), archive, original));
        const QByteArray archiveBytes = ExternalEditorFixture::read(archive);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openArchive(archive);
        auto *view = static_cast<FileTable *>(table(window));
        QTRY_COMPARE(view->rowCount(), 1);
        view->selectRow(0);
        action(window, "renameAction")->trigger();
        auto *editor = view->findChild<QLineEdit *>();
        QVERIFY(editor);
        QEventLoop completion;
        QString message;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    message = box->text();
                    box->accept();
                    completion.quit();
                }
        });
        responder.start(2);
        editor->setText(kind == "empty"           ? QString()
                        : kind == "absolute"      ? sentinel
                        : kind == "embedded-null" ? QString("truncated.txt") + QChar::Null + "tail"
                                                  : QString("../escape.txt"));
        QTest::keyClick(editor, Qt::Key_Return);
        QTimer::singleShot(5000, &completion, &QEventLoop::quit);
        completion.exec();
        QVERIFY(!message.isEmpty());
        if (kind == "empty")
            QCOMPARE(message, ToQString(HResultToMessage(E_INVALIDARG)));
        QCOMPARE(ExternalEditorFixture::read(archive), archiveBytes);
        QCOMPARE(ExternalEditorFixture::read(sentinel), QByteArray("sentinel"));
        QCOMPARE(ExternalEditorFixture::read(fixture.filePath(original)), QByteArray("control"));
        QCOMPARE(view->rowCount(), 1);
        QCOMPARE(view->model()->index(0, 0).data(Qt::UserRole + 3).toString(), original);
    }
};

QTEST_MAIN(ArchiveRenameWindowTests)
#include "archive_rename_window_tests.moc"
