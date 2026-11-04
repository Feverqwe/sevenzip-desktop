#include "7zip/PropID.h"
#include "Windows/FileFind.h"
#include "dialogs/overwrite/OverwriteDialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "settings/RegistryUtils.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QJsonArray>
#include <QJsonDocument>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString normalizedPath(const QString &path) {
    return QDir::cleanPath(
        QDir::fromNativeSeparators(path.normalized(QString::NormalizationForm_C)));
}
QString backupPath(const QString &storage, QString path) {
    path.replace(':', '_');
    return normalizedPath(storage + '/' + path);
}
bool readOnly(const QString &path) {
    NWindows::NFile::NFind::CFileInfo info;
    return info.Find(us2fs(ToUString(path))) && info.IsReadOnly();
}
bool setReadOnly(const QString &path) {
    return QFile::setPermissions(path, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup |
                                           QFile::ReadOther);
}
bool setTime(const QString &path, qint64 seconds) {
    QFile file(path);
    return file.open(QIODevice::ReadWrite) &&
           file.setFileTime(QDateTime::fromMSecsSinceEpoch(seconds * 1000),
                            QFileDevice::FileModificationTime);
}
void configure(const QString &storage, const QString &diffOutput) {
    QSettings settings(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"), QSettings::IniFormat);
    settings.setValue("FM/7vc", storage);
    settings.sync();
    SaveRegDiff(ToUString(QString("\"%1\" --ver-diff \"%2\"")
                              .arg(QCoreApplication::applicationFilePath(), diffOutput)),
                qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
}
void navigate(MainWindow &window, const QString &path) {
    auto *address = window.findChild<QLineEdit *>("archivePath");
    address->setText(path);
    QTest::keyClick(address, Qt::Key_Return);
}
} // namespace

class VerCtrlTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(UString("-"), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
    }
    void backupCommitDiffAndVersionHistory() {
        QTemporaryDir fixture;
        const QString source = fixture.filePath("рабочая папка");
        const QString storage = fixture.filePath("версии с пробелом");
        QVERIFY(QDir().mkpath(source));
        const QString path = source + "/файл с пробелом.txt";
        const QString backup = backupPath(storage, path);
        const QString output = fixture.filePath("diff.json");
        configure(storage, output);
        QVERIFY(write(path, "original"));
        QVERIFY(setTime(path, 1700000000));
        QVERIFY(setReadOnly(path));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        navigate(window, source);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        QVERIFY(action(window, "verEditAction")->isVisible());
        QVERIFY(action(window, "verEditAction")->isEnabled());
        QVERIFY(!action(window, "verCommitAction")->isVisible());
        action(window, "verEditAction")->trigger();
        QCOMPARE(read(backup), QByteArray("original"));
        QVERIFY(readOnly(backup));
        QVERIFY(!readOnly(path));
        QCOMPARE(QFileInfo(backup).lastModified(), QFileInfo(path).lastModified());
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        QVERIFY(write(path, "first edit"));
        QVERIFY(setTime(path, 1700012345));
        action(window, "verDiffAction")->trigger();
        QTRY_VERIFY(QFileInfo(output).size() > 0);
        const auto args = QJsonDocument::fromJson(read(output)).array();
        QCOMPARE(args.size(), 2);
        QCOMPARE(normalizedPath(args[0].toString()), backup);
        QCOMPARE(normalizedPath(args[1].toString()), normalizedPath(path));
        action(window, "verCommitAction")->trigger();
        QVERIFY(readOnly(path));
        QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(),
                 qint64(1700012345 / 3600 * 3600));
        action(window, "verEditAction")->trigger();
        QCOMPARE(read(backup), QByteArray("first edit"));
        const QString history =
            QFileInfo(backup).absolutePath() + "/_7vc/" + QFileInfo(path).fileName();
        QCOMPARE(read(history + "/001"), QByteArray("original"));
        QVERIFY(write(history + "/not-a-version", "ignored"));
        QVERIFY(write(path, "second edit"));
        QVERIFY(setTime(path, 1700023456));
        action(window, "verCommitAction")->trigger();
        action(window, "verEditAction")->trigger();
        QCOMPARE(read(history + "/002"), QByteArray("first edit"));
        QCOMPARE(read(backup), QByteArray("second edit"));
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
    }
    void revertAnswersAndGeometry_data() {
        QTest::addColumn<int>("answer");
        QTest::addColumn<bool>("compact");
        QTest::newRow("yes-normal") << IDYES << false;
        QTest::newRow("no-compact") << IDNO << true;
        QTest::newRow("escape-compact") << IDCANCEL << true;
    }
    void revertAnswersAndGeometry() {
        QFETCH(int, answer);
        QFETCH(bool, compact);
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source");
        QVERIFY(QDir().mkpath(source));
        const QString path = source + "/вернуть файл.txt";
        configure(fixture.filePath("backup"), fixture.filePath("diff.json"));
        QVERIFY(write(path, "original"));
        QVERIFY(setTime(path, 1700000000));
        QVERIFY(setReadOnly(path));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.resize(compact ? QSize(640, 400) : QSize(980, 620));
        window.show();
        navigate(window, source);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        QVERIFY(UiInspection::writeReport(window, compact ? "verctrl-window-compact"
                                                          : "verctrl-window-normal"));
        action(window, "verEditAction")->trigger();
        QVERIFY(write(path, "changed bytes"));
        bool asked = false, geometry = true;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            responder.stop();
            dialog->resize(compact ? QSize(560, 440) : QSize(700, 440));
            QApplication::processEvents();
            asked = !dialog->ShowExtraButtons && dialog->DefaultButton_is_NO &&
                    dialog->findChild<QPushButton *>("overwriteNo")->isDefault() &&
                    !dialog->findChild<QPushButton *>("overwriteYesAll")->isEnabled() &&
                    !dialog->findChild<QPushButton *>("overwriteNoAll")->isEnabled() &&
                    !dialog->findChild<QPushButton *>("overwriteRename")->isEnabled();
            for (auto *button : dialog->findChildren<QPushButton *>())
                if (button->isVisible())
                    geometry &= UiInspection::fullyVisible(*button, *dialog);
            for (auto *label : dialog->findChildren<QLabel *>())
                if (label->isVisible())
                    geometry &= UiInspection::fullyVisible(*label, *dialog) &&
                                UiInspection::labelFits(*label);
            geometry &= UiInspection::writeReport(
                *dialog,
                QString("ver-revert-%1-%2").arg(compact ? "compact" : "normal").arg(answer));
            if (answer == IDCANCEL)
                QTest::keyClick(dialog, Qt::Key_Escape);
            else
                dialog->findChild<QPushButton *>(answer == IDYES ? "overwriteYes" : "overwriteNo")
                    ->click();
        });
        responder.start(1);
        action(window, "verRevertAction")->trigger();
        QVERIFY(asked);
        QVERIFY(geometry);
        QCOMPARE(read(path),
                 answer == IDYES ? QByteArray("original") : QByteArray("changed bytes"));
        QCOMPARE(readOnly(path), answer == IDYES);
        if (answer == IDYES)
            QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(), qint64(1700000000));
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
        if (answer == IDYES) {
            // Identical Edit keeps the existing backup without a history entry.
            action(window, "verEditAction")->trigger();
            const QString backup = backupPath(fixture.filePath("backup"), path);
            QVERIFY(!QDir(QFileInfo(backup).absolutePath() + "/_7vc").exists());
            QVERIFY(setTime(path, 1700000001));
            // Same data with a changed timestamp reverts without overwrite UI.
            action(window, "verRevertAction")->trigger();
            QVERIFY(readOnly(path));
            QCOMPARE(QFileInfo(path).lastModified().toSecsSinceEpoch(), qint64(1700000000));
        }
    }
    void revertRetainsItsPanelAfterModalFocusChange_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<int>("answer");
        QTest::newRow("left-confirm") << 0u << IDYES;
        QTest::newRow("right-cancel") << 1u << IDCANCEL;
    }
    void revertRetainsItsPanelAfterModalFocusChange() {
        QFETCH(unsigned, pane);
        QFETCH(int, answer);
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source δ");
        const QString peerPath = fixture.filePath("peer 空");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(peerPath));
        const QString path = source + "/вернуть файл.txt";
        const QString sentinel = peerPath + "/оставить выбор.txt";
        QVERIFY(write(path, "original") && write(sentinel, "peer unchanged"));
        QVERIFY(setReadOnly(path));
        configure(fixture.filePath("backup"), fixture.filePath("diff.json"));
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        SavePanelPath(pane, ToUString(source), settings);
        SavePanelPath(1 - pane, ToUString(peerPath), settings);
        CWindowInfo frame{};
        frame.rect.right = 980;
        frame.rect.bottom = 620;
        frame.numPanels = 2;
        frame.currentPanel = pane;
        frame.splitterPos = 490;
        frame.Save(settings);
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *owner = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *peer = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - pane));
        auto *list = owner->findChild<FileTable *>("archiveTable");
        auto *peerList = peer->findChild<FileTable *>("archiveTable");
        QTRY_COMPARE(list->rowCount(), 1);
        QTRY_COMPARE(peerList->rowCount(), 1);
        list->sourcePanel->Set_AutoRefresh_Mode(false);
        peerList->sourcePanel->Set_AutoRefresh_Mode(false);
        peerList->setFocus();
        peerList->selectRow(0);
        list->setFocus();
        list->selectRow(0);
        QTRY_VERIFY(list->hasFocus());
        action(window, "verEditAction")->trigger();
        const QByteArray changed("changed version bytes");
        QVERIFY(write(path, changed));
        action(window, "refreshAction")->trigger();
        const int sizeColumn = list->columnForProperty(kpidSize);
        QCOMPARE(list->model()->index(0, sizeColumn).data(Qt::UserRole + 2).toULongLong(),
                 quint64(changed.size()));
        bool asked = false, peerFocused = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window, [&] {
            auto *dialog = dynamic_cast<COverwriteDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            responder.stop();
            asked = true;
            connect(dialog, &QDialog::finished, &window, [&] {
                window.activateWindow();
                peerList->setFocus();
                QTRY_VERIFY(peerList->hasFocus());
                peerFocused = true;
            });
            if (answer == IDCANCEL)
                QTest::keyClick(dialog, Qt::Key_Escape);
            else
                dialog->findChild<QPushButton *>("overwriteYes")->click();
        });
        responder.start(1);
        action(window, "verRevertAction")->trigger();
        QVERIFY(asked && peerFocused);
        const QByteArray expected = answer == IDYES ? QByteArray("original") : changed;
        QCOMPARE(read(path), expected);
        QCOMPARE(read(sentinel), QByteArray("peer unchanged"));
        QCOMPARE(peerList->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(peerList->selectionModel()->selectedRows().front().data().toString(),
                 QFileInfo(sentinel).fileName());
        QCOMPARE(list->selectionModel()->selectedRows().size(), 1);
        QCOMPARE(list->model()->index(0, sizeColumn).data(Qt::UserRole + 2).toULongLong(),
                 quint64(expected.size()));
    }
    void failuresPreserveSource_data() {
        QTest::addColumn<int>("failure");
        QTest::newRow("missing-backup") << 0;
        QTest::newRow("same-data-different-time") << 1;
        QTest::newRow("unavailable-backup-root") << 2;
        QTest::newRow("source-buffer-size-limit") << 3;
    }
    void failuresPreserveSource() {
        QFETCH(int, failure);
        QTemporaryDir fixture;
        const QString source = fixture.filePath("source");
        const QString storage = fixture.filePath("backup");
        QVERIFY(QDir().mkpath(source));
        const QString path = source + "/ошибка с пробелом.txt";
        QVERIFY(write(path, "original"));
        QVERIFY(setTime(path, 1700000000));
        if (failure == 3) {
            QFile sparse(path);
            QVERIFY(sparse.open(QIODevice::ReadWrite));
            QVERIFY(sparse.resize((qint64(1) << 28) + 1));
        }
        if (failure == 2)
            QVERIFY(write(storage, "occupied"));
        configure(storage, fixture.filePath("diff.json"));
        if (failure != 0)
            QVERIFY(setReadOnly(path));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        navigate(window, source);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        if (failure == 1) {
            action(window, "verEditAction")->trigger();
            QVERIFY(setTime(path, 1700000001));
        }
        bool error = false;
        QString message;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box && box->objectName() == "panelArchiveError") {
                responder.stop();
                error = box->textFormat() == Qt::PlainText;
                message = box->text();
                box->accept();
            }
        });
        responder.start(1);
        action(window, failure == 0 ? "verRevertAction"
                                    : (failure == 1 ? "verCommitAction" : "verEditAction"))
            ->trigger();
        QVERIFY(error);
        QVERIFY(!message.isEmpty());
        if (failure == 0)
            QCOMPARE(message, QStringLiteral("No file to revert"));
        if (failure == 1)
            QVERIFY(message.startsWith("Same data, but different timestamps."));
        QCOMPARE(readOnly(path), failure >= 2);
        if (failure == 3)
            QCOMPARE(QFileInfo(path).size(), (qint64(1) << 28) + 1);
        else
            QCOMPARE(read(path), QByteArray("original"));
        QCOMPARE(table(window)->selectionModel()->selectedRows().size(), 1);
    }
    void configurationAndSelectionGateCommands() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("one.txt");
        QVERIFY(write(path, "original"));
        QVERIFY(setReadOnly(path));
        ArchiveBackend backend;
        MainWindow window(&backend);
        window.show();
        navigate(window, fixture.path());
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->selectRow(0);
        QVERIFY(!action(window, "verEditAction")->isVisible());
        configure(fixture.filePath("backup"), fixture.filePath("diff.json"));
        action(window, "refreshAction")->trigger();
        QTRY_VERIFY(action(window, "verEditAction")->isVisible());
        QCOMPARE(action(window, "verEditAction")->text(), QStringLiteral("Ver Edit (&1)"));
        SaveRegDiff(UString(), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        action(window, "refreshAction")->trigger();
        QVERIFY(!action(window, "verEditAction")->isVisible());
        configure(fixture.filePath("backup"), fixture.filePath("diff.json"));
        action(window, "refreshAction")->trigger();
        QVERIFY(action(window, "verEditAction")->isVisible());
        QVERIFY(write(fixture.filePath("two.txt"), "other"));
        action(window, "refreshAction")->trigger();
        QTRY_COMPARE(table(window)->rowCount(), 2);
        table(window)->selectAll();
        QVERIFY(!action(window, "verEditAction")->isVisible());
        QVERIFY(!action(window, "verEditAction")->isEnabled());
    }
};

int main(int argc, char **argv) {
    if (argc >= 3 && QByteArray(argv[1]) == "--ver-diff") {
        QCoreApplication app(argc, argv);
        QJsonArray args;
        for (const QString &argument : app.arguments().mid(3))
            args.append(argument);
        return write(app.arguments()[2], QJsonDocument(args).toJson()) ? 0 : 1;
    }
    QApplication app(argc, argv);
    VerCtrlTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "verctrl_tests.moc"
