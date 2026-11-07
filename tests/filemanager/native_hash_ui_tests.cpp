#include "archive/codecs/archivecodeccatalog.h"
#include "filemanager/support/window_state_support.h"
#include "support/widget_dialogs.h"

class NativeHashUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QString engine_, module_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native hash-file UI remains unverified");
#else
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        version_ = backend.codecCatalog()->version();
#endif
    }
    void manifestOpensAsAnOrdinaryFile() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString name = QStringLiteral("имя с пробелом.txt");
        const QByteArray digest =
            QCryptographicHash::hash("123456789", QCryptographicHash::Sha256).toHex();
        const QString manifest = temporary.filePath("checksums.sha256");
        QFile hashes(manifest);
        QVERIFY(hashes.open(QIODevice::WriteOnly));
        const QByteArray data = digest + "  " + name.toUtf8() + "\n";
        QCOMPARE(hashes.write(data), qint64(data.size()));
        hashes.close();

        ArchiveBackend backend(nullptr, engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        QUrl opened;
        MainWindow window(&backend, [&](const QUrl &url) {
            opened = url;
            return true;
        });
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        QVERIFY(address);
        address->setText(temporary.path());
        emit address->returnPressed();
        QTRY_VERIFY(filesystemRow(window, "checksums.sha256") >= 0);
        table(window)->selectRow(filesystemRow(window, "checksums.sha256"));
        auto *open = action(window, "openItemAction");
        QTRY_VERIFY(open->isEnabled());
        open->trigger();
        QCOMPARE(QFileInfo(opened.toLocalFile()).canonicalFilePath(),
                 QFileInfo(manifest).canonicalFilePath());

        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                box->accept();
        });
        responder.start(1);
        window.openArchive(manifest);
        responder.stop();
        QTRY_VERIFY(filesystemRow(window, "checksums.sha256") >= 0);
        QVERIFY(hashes.open(QIODevice::ReadOnly));
        QCOMPARE(hashes.readAll(), data);
    }
};
QTEST_MAIN(NativeHashUiTests)
#include "native_hash_ui_tests.moc"
