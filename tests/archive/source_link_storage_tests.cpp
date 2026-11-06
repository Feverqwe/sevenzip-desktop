#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/source_metadata_update_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "fileops/native/FileDir.h"
#include "localization/LangUtils.h"
#include "platform/support/native_link_fixture.h"
#include "settings/RegistryUtils.h"
#include "support/archive_expert.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QStandardPaths>

namespace {
QByteArray readFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class SourceLinkStorageTests final : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
  private slots:
    void optionsTransactionAndGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize size : {QSize(540, 480), QSize(480, 430)})
                QTest::newRow(qPrintable(language + QString::number(size.width())))
                    << language << size;
    }
    void optionsTransactionAndGeometry() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module required for source format capabilities.");
#else
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray oldSettings = qgetenv("SEVENZIP_SETTINGS_PATH");
        const QString settings = fixture.filePath("settings.ini");
        qputenv("SEVENZIP_SETTINGS_PATH", settings.toUtf8());
        const auto restoreSettings = qScopeGuard([&] {
            if (oldSettings.isNull())
                qunsetenv("SEVENZIP_SETTINGS_PATH");
            else
                qputenv("SEVENZIP_SETTINGS_PATH", oldSettings);
            ReloadLang();
        });
        SaveRegLang(UString(language.toStdWString().c_str()), settings);
        ReloadLang(settings);
        const QString source = fixture.filePath("source.txt");
        writeFile(source, "payload");
        ArchiveBackend backend;
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE)));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        CreateArchiveDialog form(fixture.path(), {source}, nullptr, backend.codecCatalog());
        form.findChild<QComboBox *>("archiveFormat")->setCurrentText("tar");
        bool inspected = false;
        const auto changeOptions = [&](bool accept) {
            QVERIFY(EditArchiveMetadata(form, accept, [&](QDialog &active) {
                auto *dialog = &active;
                dialog->resize(size);
                QApplication::processEvents();
                // Native control/font minima may enlarge a compact request.
                // The contract is reachable, unclipped controls below.
                auto *sym = dialog->findChild<QCheckBox *>("IDX_COMPRESS_NT_SYM_LINKS");
                auto *hard = dialog->findChild<QCheckBox *>("IDX_COMPRESS_NT_HARD_LINKS");
                QVERIFY(sym && hard && sym->isVisible() && hard->isVisible());
                dialog->findChild<QScrollArea *>("dialogContentScroll")->ensureWidgetVisible(hard);
                QApplication::processEvents();
                QVERIFY(UiInspection::fullyVisible(*sym, *dialog));
                QVERIFY(UiInspection::fullyVisible(*hard, *dialog));
                QVERIFY(sym->width() >= sym->sizeHint().width());
                QVERIFY(hard->width() >= hard->sizeHint().width());
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                QVERIFY(buttons && UiInspection::fullyVisible(*buttons, *dialog));
                QVERIFY(UiInspection::writeReport(
                    *dialog, QString("source-metadata-%1-%2").arg(language).arg(size.width())));
                sym->setChecked(true);
                hard->setChecked(true);
                inspected = true;
            }));
        };
        changeOptions(false);
        QVERIFY(inspected);
        QVERIFY(!form.compressionOptions().SymLinks.Val);
        changeOptions(true);
        QVERIFY(form.compressionOptions().SymLinks.Val);
        QVERIFY(form.compressionOptions().HardLinks.Val);
        QVERIFY(!form.compressionOptions().NtSecurity.Val);
        QVERIFY(!form.compressionOptions().AltStreams.Val);
        NCompression::CInfo saved;
        saved.Load(settings);
        QVERIFY(!saved.SymLinks.Val && !saved.HardLinks.Val);
        form.findChild<QComboBox *>("archiveFormat")->setCurrentText("zip");
        QVERIFY(!form.compressionOptions().SymLinks.Val);
        QVERIFY(!form.compressionOptions().HardLinks.Val);
        form.findChild<QComboBox *>("archiveFormat")->setCurrentText("tar");
        QVERIFY(form.compressionOptions().SymLinks.Val);
        form.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(form.result(), int(QDialog::Accepted));
        saved.Load(settings);
        QVERIFY(saved.SymLinks.Val && saved.HardLinks.Val);
        CreateArchiveDialog restarted(fixture.path(), {source}, nullptr, backend.codecCatalog());
        QCOMPARE(restarted.findChild<QComboBox *>("archiveFormat")->currentText(), QString("tar"));
        QVERIFY(restarted.compressionOptions().SymLinks.Val);
        QVERIFY(restarted.compressionOptions().HardLinks.Val);
        restarted.reject();
#endif
    }
    void sourceAddStoresLinksAndDeletesOnlyAfterPublication_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<HRESULT>("failure");
        QTest::addColumn<bool>("hardLinks");
        for (const QString &format : {QString("tar")}) {
            QTest::newRow(qPrintable(format + "-success")) << format << HRESULT(S_OK) << false;
            QTest::newRow(qPrintable(format + "-failure")) << format << HRESULT(E_FAIL) << false;
            QTest::newRow(qPrintable(format + "-cancel")) << format << HRESULT(E_ABORT) << false;
        }
        QTest::newRow("tar-hard-links") << QString("tar") << HRESULT(S_OK) << true;
        QTest::newRow("zip-original-capability-refusal")
            << QString("zip") << HRESULT(E_NOTIMPL) << false;
        QTest::newRow("7z-original-capability-refusal")
            << QString("7z") << HRESULT(E_NOTIMPL) << false;
    }
    void sourceAddStoresLinksAndDeletesOnlyAfterPublication() {
#if !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Requires official module.");
#else
        QFETCH(QString, format);
        QFETCH(HRESULT, failure);
        QFETCH(bool, hardLinks);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        const QString archive = fixture.filePath("result." + format);
        writeFile(fixture.filePath("keep.txt"), "original");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine, {"a", "-t" + format, "--", archive, "keep.txt"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        const QByteArray original = readFile(archive);
        const QString root = fixture.filePath("источник с пробелом");
        QVERIFY(QDir().mkpath(root));
        const QString target = fixture.filePath("outside/цель.txt");
        QVERIFY(QDir().mkpath(QFileInfo(target).absolutePath()));
        writeFile(target, "outside bytes");
        writeFile(root + "/payload.bin", QByteArray(1024 * 1024, 'x'));
        if (hardLinks)
            QVERIFY(NWindows::NFile::NDir::MyCreateHardLink(
                us2fs((root + "/hard alias.bin").toStdWString().c_str()),
                us2fs((root + "/payload.bin").toStdWString().c_str())));
        QVERIFY(CreateNativeSymbolicLink(root + "/file link", target));
        QVERIFY(
            CreateNativeSymbolicLink(root + "/directory link", QFileInfo(target).absolutePath()));
        QVERIFY(CreateNativeSymbolicLink(root + "/dangling link", fixture.filePath("missing")));
        QVERIFY(CreateNativeSymbolicLink(root + "/cycle link", "cycle link"));
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(QString::fromUtf8(SEVENZIP_CODEC_MODULE)));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource && resource->HasUpdateService());
        QCOMPARE(folder->LoadItems(), S_OK);
        CompressionOptions options;
        options.Level = 0;
        options.SymLinks.SetTrueTrue();
        if (hardLinks)
            options.HardLinks.SetTrueTrue();
        options.DeleteAfterCompressing = !hardLinks;
        resource->SetCompressionOptions(options);
        CMyComPtr<IFolderOperations> operations;
        QCOMPARE(opening.Folder.QueryInterface(IID_IFolderOperations, &operations), S_OK);
        CLinkUpdateWorker worker(operations, root);
        worker.Failure = failure == E_NOTIMPL ? S_OK : failure;
        worker.DisableUserQuestions = true;
        // PanelCopy supplies this default call: the resource must identify the
        // native metadata phase itself, before any backend process completes.
        auto transport = resource->CreateProgressTransport(worker);
        QVERIFY(transport);
        bool stableWhilePaused = false, resumed = false;
        if (failure != E_NOTIMPL) {
            QCOMPARE(transport->SetPaused(true), S_OK);
            worker.Sync.Set_Paused(true);
            QTimer::singleShot(100, &worker, [&] {
                stableWhilePaused = !backend.busy() && readFile(archive) == original;
                resumed = transport->SetPaused(false) == S_OK;
                worker.Sync.Set_Paused(false);
            });
        }
        QCOMPARE(worker.Create(L"Source link storage", nullptr), S_OK);
        if (failure != E_NOTIMPL)
            QVERIFY(stableWhilePaused && resumed);
        QCOMPARE(worker.Result, failure);
        QCOMPARE(readFile(target), QByteArray("outside bytes"));
        if (failure != S_OK) {
            QCOMPARE(worker.Observed, failure != E_NOTIMPL);
            QCOMPARE(readFile(archive), original);
            QVERIFY(QFileInfo(root + "/dangling link").isSymLink());
            QVERIFY(QFileInfo(root + "/directory link").isSymLink());
            QVERIFY(QFileInfo(root + "/cycle link").isSymLink());
            QCOMPARE(readFile(root + "/payload.bin"), QByteArray(1024 * 1024, 'x'));
            return;
        }
        // Publication reopens through the existing listing transport. There is
        // one listing process, with no staging/update process for this writer.
        QCOMPARE(QFileInfo::exists(root), hardLinks);
        QProcess extractor;
        const QString output = fixture.filePath("output");
        extractor.start(engine, {"x", "-y", "-snld", "-o" + output, "--", archive});
        QVERIFY(extractor.waitForFinished(10000));
        QCOMPARE(extractor.exitCode(), 0);
        QCOMPARE(readFile(output + "/keep.txt"), QByteArray("original"));
        const QString restored = output + "/" + QFileInfo(root).fileName();
        QCOMPARE(readFile(restored + "/payload.bin"), QByteArray(1024 * 1024, 'x'));
        if (hardLinks) {
            QVERIFY(
                NativeFilesShareIdentity(restored + "/payload.bin", restored + "/hard alias.bin"));
            QCOMPARE(readFile(root + "/hard alias.bin"), QByteArray(1024 * 1024, 'x'));
        }
        for (const QString &name : {QString("file link"), QString("directory link"),
                                    QString("dangling link"), QString("cycle link")})
            QVERIFY2(QFileInfo(restored + "/" + name).isSymLink(), qPrintable(name));
#endif
    }
};
QTEST_MAIN(SourceLinkStorageTests)
#include "source_link_storage_tests.moc"
