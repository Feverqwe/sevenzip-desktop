#include "7zip/PropID.h"
#include "archive/backend/archivebackend.h"
#include "dialogs/copy/CopyDialog.h"
#include "dialogs/messages/MessagesDialog.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/mainwindow.h"
#include "formats/exfat/support/exfat_fixture.h"
#include "localization/qtlanguageservices.h"
#include "settings/RegistryUtils.h"
#include "sparsefixtures.h"
#include "support/codec_runtime.h"
#include "support/widget_dialogs.h"
#include "ui/support/layout_inspection.h"
#include <QAction>
#include <QComboBox>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QtTest>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
int Row(FileTable &table, const QString &name) {
    for (int row = 0; row < table.rowCount(); ++row)
        if (table.model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
QJsonObject Fixture(const QString &name) {
    for (const auto &fixture : SparseFixtureManifest()["fixtures"].toArray())
        if (fixture.toObject()["name"].toString() == name)
            return fixture.toObject();
    return {};
}
} // namespace
class SparseImageUiTests final : public QObject {
    Q_OBJECT
    QTemporaryDir runtime_;
    QString engine_, module_;
  private slots:
    void initTestCase() {
        QVERIFY(runtime_.isValid());
        QVERIFY(
            CopyCodecRuntime(QDir(QString::fromUtf8(SEVENZIP_DMG_RUNTIME)), QDir(runtime_.path())));
        QVERIFY(QDir(runtime_.path()).mkpath("Formats"));
#ifdef Q_OS_WIN
        const QString engine = "7z.exe", module = "7z.dll", suffix = ".dll";
#else
        const QString engine = "7z", module = "7z.so", suffix = ".so";
#endif
        engine_ = runtime_.filePath(engine);
        module_ = runtime_.filePath(module);
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_MODULE),
                            runtime_.filePath("Formats/EncryptedDmg" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_HFS_MODULE),
                            runtime_.filePath("Formats/Hfs" + suffix)));
        QVERIFY(QFile::copy(QString::fromUtf8(SEVENZIP_DMG_EXFAT_MODULE),
                            runtime_.filePath("Formats/Exfat" + suffix)));
    }
    void browseAndCopy_data() {
        QTest::addColumn<QString>("name");
        for (const auto &name : {"native-sparse-0", "native-sparse-256", "native-sparsebundle-0",
                                 "native-sparsebundle-256"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void browseAndCopy() {
        QFETCH(QString, name);
        QTemporaryDir inputs, output;
        const auto fixture = Fixture(name);
        const auto source = UnpackSparseFixture(fixture, inputs);
        QVERIFY(!source.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        MainWindow window(&backend);
        window.openPath(inputs.path());
        window.show();
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        bool cancel = fixture["bits"].toInt() != 0;
        int prompts = 0;
        QString error;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            auto *active = dialogs.activeDialog();
            if (auto *password = dynamic_cast<CPasswordDialog *>(active)) {
                if (cancel) {
                    password->reject();
                    return;
                }
                password->findChild<QLineEdit *>("passwordEdit")
                    ->setText(++prompts == 1 ? "wrong" : "fixture-password");
                password->accept();
            } else if (auto *message = qobject_cast<QMessageBox *>(active)) {
                error += message->text();
                message->accept();
            }
        });
        automation.start(10);
        if (cancel) {
            window.openPath(source);
            QVERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
            cancel = false;
        }
        QTRY_VERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
        const auto index = table->model()->index(Row(*table, QFileInfo(source).fileName()), 0);
        table->selectRow(index.row());
        QVERIFY(QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, index)));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const int folder = Row(*table, "files");
        QVERIFY(folder >= 0);
        QVERIFY(QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, table->model()->index(folder, 0))));
        const auto filename = QString::fromUtf8("hello 空.txt");
        QTRY_VERIFY(Row(*table, filename) >= 0);
        table->selectRow(Row(*table, filename));
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
        window.extractTo(output.path(), true, {}, options);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(prompts, fixture["bits"].toInt() ? 2 : 0);
        QDirIterator files(output.path(), QDir::Files, QDirIterator::Subdirectories);
        QByteArray contents;
        while (files.hasNext()) {
            QFile file(files.next());
            if (QFileInfo(file).fileName() == filename && file.open(QIODevice::ReadOnly))
                contents = file.readAll();
        }
        QCOMPARE(contents, QByteArray("Sparse image fixture\n"));
    }
    void browseApfsDirectories_data() {
        QTest::addColumn<QString>("directory");
        QTest::addColumn<QString>("language");
        for (const QString &language : {QString("en"), QString("ru")}) {
            QTest::newRow(qPrintable(language + " nested")) << QString("directory-00") << language;
            QTest::newRow(qPrintable(language + " hardlink"))
                << QString("directory-01") << language;
#ifndef Q_OS_WIN
            QTest::newRow(qPrintable(language + " symlink")) << QString("directory-02") << language;
#endif
            QTest::newRow(qPrintable(language + " file-with-attributes"))
                << QString("root.txt") << language;
            QTest::newRow(qPrintable(language + " deep-file")) << QString("deep-file") << language;
            QTest::newRow(qPrintable(language + " deep-folder"))
                << QString("deep-folder") << language;
        }
    }
    void browseApfsDirectories() {
        QFETCH(QString, directory);
        QFETCH(QString, language);
        SaveRegLang(UString(language == "en" ? L"-" : L"ru"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtLoadTranslation(language == "en" ? "-" : "ru");
        QTemporaryDir input, output;
        const auto path = DirectorySparseBundle(input);
        QVERIFY(!path.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        MainWindow window(&backend);
        window.show();
        QString errors;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            if (auto *password = dynamic_cast<CPasswordDialog *>(dialogs.activeDialog())) {
                password->findChild<QLineEdit *>("passwordEdit")->setText("fixture-password");
                password->accept();
            } else if (auto *copy = dynamic_cast<CopyDialog *>(dialogs.activeDialog())) {
                copy->findChild<QComboBox *>("copyDestination")->setEditText(output.path() + '/');
                copy->accept();
            } else if (auto *messages = dynamic_cast<CMessagesDialog *>(dialogs.activeDialog())) {
                if (messages->Messages)
                    FOR_VECTOR(i, *messages->Messages)
                errors += QString::fromWCharArray((*messages->Messages)[i]);
                messages->accept();
            } else if (auto *message = qobject_cast<QMessageBox *>(dialogs.activeDialog())) {
                errors += message->text() + message->informativeText();
                message->accept();
            }
        });
        automation.start(10);
        window.openPath(path);
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        if (directory == "deep-file" || directory == "deep-folder") {
            const auto parents = directory == "deep-file" ? QStringList{"directory-00", "nested"}
                                                          : QStringList{"directory-00"};
            for (const QString &name : parents) {
                const auto row = Row(*table, name);
                QVERIFY(row >= 0);
                QVERIFY(
                    QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                              Q_ARG(QModelIndex, table->model()->index(row, 0))));
            }
            const auto name =
                directory == "deep-file" ? QString::fromUtf8("данные 空.txt") : QString("nested");
            QVERIFY(Row(*table, name) >= 0);
            table->selectRow(Row(*table, name));
            auto *copy = window.findChild<QAction *>("copyAction");
            QVERIFY(copy && copy->isEnabled());
            copy->trigger();
            QVERIFY2(errors.isEmpty(), qPrintable(errors));
            QFile copied(output.filePath(
                directory == "deep-file" ? name : name + QString::fromUtf8("/данные 空.txt")));
            QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(), QByteArray("Nested APFS payload\n"));
#ifndef Q_OS_WIN
            struct stat metadata{};
            QVERIFY(::lstat(QFile::encodeName(copied.fileName()), &metadata) == 0);
            QCOMPARE(metadata.st_mode & 0777, mode_t(0644));
#ifdef Q_OS_MACOS
            QCOMPARE(metadata.st_flags & UF_IMMUTABLE, 0u);
#endif
#endif
            return;
        }
        QVERIFY(Row(*table, directory) >= 0);
        QVERIFY(Row(*table, "root.txt") >= 0);
        QVERIFY(Row(*table, "file-000.txt") < 0);
        QVERIFY(table->rowCount() < 100);
        if (directory == "root.txt") {
            table->selectRow(Row(*table, directory));
            ExtractionOptions options;
            options.overwriteMode = ExtractionOptions::OverwriteMode::OverwriteExisting;
            window.extractTo(output.path(), true, {}, options);
            QVERIFY2(errors.isEmpty(), qPrintable(errors));
            QCOMPARE(QDir(output.path()).entryList(QDir::Files), QStringList{"root.txt"});
            QFile copied(output.filePath("root.txt"));
            QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(), QByteArray("APFS root payload\n"));
            return;
        }
        table->selectRow(Row(*table, directory));
        const int sizeColumn = table->columnForProperty(kpidSize);
        QVERIFY(sizeColumn >= 0);
        QVERIFY(!table->model()
                     ->index(Row(*table, directory), sizeColumn)
                     .data(Qt::UserRole + 2)
                     .isValid());
        auto *calculate = window.findChild<QAction *>("calculateDirectorySizeAction");
        QVERIFY(calculate);
        QTRY_VERIFY(calculate->isEnabled());
        if (directory == "directory-00") {
            auto *toolbar = window.findChild<QToolBar *>("archiveToolbar");
            auto *button = toolbar->widgetForAction(calculate);
            QVERIFY(button);
            for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
                window.resize(size);
                QTest::qWait(10);
                QVERIFY(button->isVisible());
                QVERIFY(UiInspection::fullyVisible(*button, window));
                QVERIFY(UiInspection::writeReport(
                    window, QStringLiteral("apfs-size-%1-%2").arg(language).arg(size.width())));
            }
        }
        calculate->trigger();
        QTRY_VERIFY(!calculate->isEnabled());
        const auto size =
            table->model()->index(Row(*table, directory), sizeColumn).data(Qt::UserRole + 2);
        QVERIFY(size.isValid());
        QVERIFY(size.toULongLong() > 0);
        // Completing one subtree must not imply that a sibling's size is known.
        table->selectRow(Row(*table, "directory-11"));
        QTRY_VERIFY(calculate->isEnabled());
        const int row = Row(*table, directory);
        QVERIFY(QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection,
                                          Q_ARG(QModelIndex, table->model()->index(row, 0))));
        QTRY_VERIFY(Row(*table, "file-000.txt") >= 0);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        auto *up = window.findChild<QAction *>("upAction");
        QVERIFY(up && up->isEnabled());
        up->trigger();
        QTRY_VERIFY(Row(*table, directory) >= 0);
        table->selectRow(Row(*table, directory));
        auto *copy = window.findChild<QAction *>("copyAction");
        QVERIFY(copy && copy->isEnabled());
        copy->trigger();
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
#ifndef Q_OS_WIN
        struct stat metadata{};
        QVERIFY(::lstat(QFile::encodeName(output.filePath(directory)), &metadata) == 0);
        QCOMPARE(metadata.st_mode & 0777, mode_t(0755));
        QVERIFY(::lstat(QFile::encodeName(output.filePath(directory + "/file-000.txt")),
                        &metadata) == 0);
        QCOMPARE(metadata.st_mode & 0777, mode_t(0644));
#ifdef Q_OS_MACOS
        QCOMPARE(metadata.st_flags & UF_IMMUTABLE, 0u);
#endif
        if (directory == "directory-02") {
            const auto link = QFile::encodeName(output.filePath(directory + "/root-link"));
            QVERIFY(::lstat(link, &metadata) == 0);
            QVERIFY(S_ISLNK(metadata.st_mode));
            char target[128]{};
            const auto length = ::readlink(link, target, sizeof(target));
            QCOMPARE(QByteArray(target, length), QByteArray("../root.txt"));
            return;
        }
#endif
        QFile first(output.filePath(directory + "/file-000.txt"));
        QVERIFY(first.open(QIODevice::ReadOnly));
        QCOMPARE(first.readAll(), QByteArray(directory.endsWith("00") ? "Directory 0, file 0\n"
                                                                      : "Directory 1, file 0\n"));
        QFile special(output.filePath(directory + (directory.endsWith("00")
                                                       ? QString::fromUtf8("/nested/данные 空.txt")
                                                       : "/linked-root.txt")));
        QVERIFY(special.open(QIODevice::ReadOnly));
        QCOMPARE(special.readAll(), QByteArray(directory.endsWith("00") ? "Nested APFS payload\n"
                                                                        : "APFS root payload\n"));
        QVERIFY(!QFileInfo::exists(output.filePath("directory-11")));
    }
    void damagedWarning_data() {
        QTest::addColumn<bool>("accept");
        QTest::addColumn<QString>("language");
        for (const QString &language : {"en", "ru"})
            for (bool accept : {false, true})
                QTest::newRow(qPrintable(language + (accept ? "-read-only" : "-decline")))
                    << accept << language;
    }
    void damagedWarning() {
        QFETCH(bool, accept);
        QFETCH(QString, language);
        SaveRegLang(UString(language == "en" ? L"-" : L"ru"),
                    qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QtLoadTranslation(language == "en" ? "-" : "ru");
        QTemporaryDir inputs;
        const auto source = DamagedSparseBundle(inputs);
        QVERIFY(!source.isEmpty());
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 15000);
        MainWindow window(&backend);
        window.openPath(inputs.path());
        window.show();
        int warnings = 0;
        bool geometry = true;
        QString detail, error;
        WidgetDialogFixture dialogs;
        QTimer automation;
        dialogs.connectResponder(automation, &window, [&] {
            auto *active = dialogs.activeDialog();
            if (auto *password = dynamic_cast<CPasswordDialog *>(active)) {
                password->findChild<QLineEdit *>("passwordEdit")->setText("fixture-password");
                password->accept();
            } else if (auto *message = qobject_cast<QMessageBox *>(active)) {
                if (message->objectName() == "filesystemReadLimitationsWarning") {
                    ++warnings;
                    detail = message->informativeText();
                    message->setProperty("uiInspection_language", language);
                    for (const QString &size : {"normal", "compact"}) {
                        if (size == "compact")
                            message->resize(message->minimumSizeHint());
                        message->layout()->activate();
                        for (auto *label : message->findChildren<QLabel *>())
                            if (label->isVisible())
                                geometry &= UiInspection::fullyVisible(*label, *message) &&
                                            UiInspection::labelFits(*label);
                        for (auto *button : message->findChildren<QPushButton *>())
                            if (button->isVisible())
                                geometry &= UiInspection::fullyVisible(*button, *message);
                        message->setProperty("uiInspection_state", "damaged-sparsebundle-" + size);
                        geometry &= UiInspection::writeReport(*message, "sparsebundle-damage-" +
                                                                            language + "-" + size);
                    }
                    if (accept)
                        message->findChild<QPushButton *>("deviceOpenWithoutRecoveryButton")
                            ->click();
                    else
                        message->reject();
                } else {
                    error += message->text();
                    message->accept();
                }
            }
        });
        automation.start(10);
        window.openPath(source);
        QCOMPARE(warnings, 1);
        QVERIFY(detail.contains(language == "en" ? "damaged or unreadable"
                                                 : QString::fromUtf8("повреждены или недоступны")));
        QVERIFY(geometry);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        auto *table = window.findChild<FileTable *>("archiveTable");
        QVERIFY(table);
        const ExfatFixture names;
        if (accept)
            QVERIFY(Row(*table, names.fragmentedName) >= 0);
        else
            QVERIFY(Row(*table, QFileInfo(source).fileName()) >= 0);
    }
};
QTEST_MAIN(SparseImageUiTests)
#include "sparse_image_ui_tests.moc"
