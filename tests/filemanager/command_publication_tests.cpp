#include "filemanager/support/window_state_support.h"

class CommandPublicationTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void navigationDuringActionChangeDiscardsOldAvailability() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source folder");
        const QString destination = fixture.filePath("empty folder");
        QVERIFY(QDir().mkpath(source) && QDir().mkpath(destination));
        const QString name = QStringLiteral("данные.001");
        QFile input(QDir(source).filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("data"), qint64(4));
        input.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        window.openPath(source);
        auto *view = table(window);
        const int row = filesystemRow(window, name);
        QVERIFY(row >= 0);
        view->clearSelection();
        view->selectionModel()->setCurrentIndex({}, QItemSelectionModel::NoUpdate);
        const QList<QAction *> commands = {action(window, "renameAction"),
                                           action(window, "deleteAction"),
                                           action(window, "archiveInfoAction")};
        for (const auto *command : commands)
            QVERIFY(!command->isEnabled());
        bool redirecting = false, redirected = false;
        QStringList obsolete;
        QObject observer;
        for (auto *command : commands)
            connect(command, &QAction::changed, &observer, [&, command] {
                if (!command->isEnabled() || redirecting)
                    return;
                if (redirected) {
                    obsolete << command->objectName();
                    return;
                }
                redirecting = true;
                window.openPath(destination);
                redirected = true;
                redirecting = false;
            });
        view->selectionModel()->setCurrentIndex(view->model()->index(row, 0),
                                                QItemSelectionModel::ClearAndSelect |
                                                    QItemSelectionModel::Rows);
        QVERIFY(redirected);
        QCOMPARE(QDir::cleanPath(window.findChild<QLineEdit *>("archivePath")->text()),
                 QFileInfo(destination).canonicalFilePath());
        QVERIFY2(obsolete.isEmpty(), qPrintable(obsolete.join(", ")));
        for (const auto *command : commands)
            QVERIFY(!command->isEnabled());
        for (auto *command : commands)
            disconnect(command, nullptr, &observer, nullptr);
        window.openPath(source);
        const int restored = filesystemRow(window, name);
        QVERIFY(restored >= 0);
        view->selectionModel()->setCurrentIndex(view->model()->index(restored, 0),
                                                QItemSelectionModel::ClearAndSelect |
                                                    QItemSelectionModel::Rows);
        for (const auto *command : commands)
            QVERIFY(command->isEnabled());
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("data"));
    }
};

QTEST_MAIN(CommandPublicationTests)
#include "command_publication_tests.moc"
