#include "archive/codecs/archivecodeccatalog.h"
#include "filemanager/support/window_state_support.h"
#include "formats/rar/support/rar_memory_fixture.h"
#include "support/drag_event_position.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QDragEnterEvent>

namespace {
class CObservedBackend final : public ArchiveBackend {
  public:
    unsigned ConsoleExtracts = 0, ConsoleTests = 0;
    explicit CObservedBackend(const QString &engine) : ArchiveBackend(nullptr, engine) {}
    void extract(const QString &archive, const QString &destination, const QStringList &names,
                 const QString &password, const ExtractionOptions &options) override {
        ++ConsoleExtracts;
        ArchiveBackend::extract(archive, destination, names, password, options);
    }
    void test(const QString &archive, const QString &password) override {
        ++ConsoleTests;
        ArchiveBackend::test(archive, password);
    }
};
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class NativeRarUiTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QString engine_, module_;
    quint32 version_ = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native RAR commands unverified");
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
    void commandsUseSourceHandler_data() {
        QTest::addColumn<bool>("selected");
        QTest::addColumn<int>("overwrite");
        for (bool selected : {false, true})
            for (auto mode : {ExtractionOptions::OverwriteMode::Ask,
                              ExtractionOptions::OverwriteMode::OverwriteExisting,
                              ExtractionOptions::OverwriteMode::SkipExisting,
                              ExtractionOptions::OverwriteMode::RenameExtracted,
                              ExtractionOptions::OverwriteMode::RenameExisting,
                              ExtractionOptions::OverwriteMode::RejectExisting})
                QTest::newRow(qPrintable(QString("%1-%2").arg(selected).arg(int(mode))))
                    << selected << int(mode);
    }
    void commandsUseSourceHandler() {
        QFETCH(bool, selected);
        QFETCH(int, overwrite);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("compressed.rar");
        QVERIFY(QFile::copy(QString::fromUtf8(RAR_COMPRESSED_FIXTURE), archive));
        const auto original = read(archive);
        CObservedBackend backend(engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 1);
        table(window)->setCurrentCell(0, 0);
        table(window)->selectRow(0);
        QStringList errors;
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                if (box->icon() == QMessageBox::Critical || box->icon() == QMessageBox::Warning)
                    errors.append(box->text());
                box->accept();
            } else if (auto *dialog = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog())) {
                auto *pause = dialog->findChild<QPushButton *>("IDB_PAUSE");
                if (pause && !pause->isVisible())
                    dialog->findChild<QPushButton *>("IDCANCEL")->click();
            }
        });
        responder.start(2);
        const QString output = fixture.filePath("output");
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode(overwrite);
        window.extractTo(output, selected, {}, options);
        QTRY_VERIFY(QFileInfo::exists(output + "/test.bin"));
        const auto bytes = read(output + "/test.bin");
        QCOMPARE(bytes.size(), 1200);
        QCOMPARE(CrcCalc(bytes.constData(), size_t(bytes.size())), UInt32(0x7CCA70CD));
        window.testArchive();
        QCOMPARE(backend.ConsoleExtracts, 0u);
        QCOMPARE(backend.ConsoleTests, 0u);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join("; ")));
        QCOMPARE(read(archive), original);
    }
    void readOnlyArchiveRejectsFileDrag() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("compressed.rar");
        QVERIFY(QFile::copy(QString::fromUtf8(RAR_COMPRESSED_FIXTURE), archive));
        QFile input(fixture.filePath("new entry.txt"));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("payload"), qint64(7));
        input.close();

        CObservedBackend backend(engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        auto *files = table(window);
        QTRY_VERIFY(files->isEnabled() && files->rowCount() == 1);
        QCOMPARE(files->model()->index(0, 0).data().toString(), QString("test.bin"));

        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(input.fileName())});
        QDragEnterEvent enter(DragEventPosition(QPoint(5, 5)), Qt::CopyAction, &mime,
                              Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(files->viewport(), &enter);
        QVERIFY(!enter.isAccepted());
    }

    void unsafePathRefusedBeforeExtraction() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("unsafe.rar");
        const auto original = RarMemoryFixture::archive(1, QStringLiteral("../escape.bin"));
        QFile file(archive);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        CObservedBackend backend(engine_);
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        window.openArchive(archive);
        QTRY_COMPARE(table(window)->rowCount(), 2);
        bool refused = false;
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                refused = QDir::fromNativeSeparators(box->text()).contains("../escape.bin");
                box->accept();
            }
        });
        responder.start(2);
        const QString output = fixture.filePath("output");
        ExtractionOptions options;
        options.overwriteMode = ExtractionOptions::OverwriteMode::Ask;
        window.extractTo(output, false, {}, options);
        QVERIFY(refused);
        QCOMPARE(backend.ConsoleExtracts, 0u);
        QVERIFY(!QFileInfo::exists(fixture.filePath("escape.bin")));
        QVERIFY(QDir(output).entryList(QDir::Files).isEmpty());
        QCOMPARE(read(archive), original);
    }
};
QTEST_MAIN(NativeRarUiTests)
#include "native_rar_ui_tests.moc"
