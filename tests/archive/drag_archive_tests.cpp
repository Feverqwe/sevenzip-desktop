#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/support/native_archive_fixture.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragtransport.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/providers/FSFolder.h"
#include "platform/temp/qtownedtempregistry.h"
#include <QDrag>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <new>
#include <stdexcept>

class DragArchiveTests : public QObject {
    Q_OBJECT
    QByteArray previousRegistry;
    QTemporaryDir registryFixture;
    static QByteArray Read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool BindArchive(CPanel &panel, ArchiveBackend &backend, const QString &path,
                            const QString &name, bool writable) {
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> owner = resource;
        if (NativeArchiveFixture::Attach(backend) != S_OK)
            return false;
        std::shared_ptr<ArchiveNativeOpen> reader;
        if (backend.openNativeArchive(path, "zip", nullptr, reader) != S_OK)
            return false;
        resource->AttachCodecCatalog(backend.codecCatalog());
        resource->AttachNativeOpen(std::move(reader));
        resource->UseNativeMetadata();
        resource->AttachExtractService(backend, "zip");
        if (writable) {
            resource->AttachUpdateService(backend, "zip");
            resource->AttachReopenService(backend, "zip");
        }
        CObjectVector<CPanelItem> items;
        CPanelItem item;
        item.RelPath = name;
        items.Add(item);
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        if (agent->Init(metadata, UString("zip"), owner) != S_OK)
            return false;
        CMyComPtr<IFolderFolder> folder;
        if (agent->BindToRootFolder(&folder) != S_OK || folder->LoadItems() != S_OK)
            return false;
        panel.SetNewFolder(folder);
        return panel.IsArcFolder();
    }
  private slots:
    void init() {
        previousRegistry = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        // Unconfirmed receivers deliberately retain their registry records. Each
        // disposable row needs its own registry, including when native temp names
        // and OS file identities are reused after fixture cleanup.
        qputenv(
            "SEVENZIP_OWNED_TEMP_REGISTRY",
            registryFixture.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8());
    }
    void cleanup() { qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", previousRegistry); }
    void nativeArchiveDropUsesCorrectExecutionOwner_data() {
        QTest::addColumn<bool>("archiveTarget");
        QTest::newRow("archive-to-disk") << false;
        QTest::newRow("archive-to-archive") << true;
    }
    void nativeArchiveDropUsesCorrectExecutionOwner() {
        QFETCH(bool, archiveTarget);
        QTemporaryDir root;
        const QString name = QStringLiteral("из архива с пробелом.bin");
        const QByteArray payload("transfer\0bytes", 14);
        QFile input(root.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        QFile seed(root.filePath("untouched.txt"));
        QVERIFY(seed.open(QIODevice::WriteOnly));
        seed.write("untouched bytes");
        seed.close();
        const QString sourceZip = root.filePath("source.zip");
        const QString targetZip = root.filePath("target.zip");
        const QString destination = root.filePath("destination");
        QVERIFY(QDir().mkpath(destination));
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        for (const auto &pair :
             {qMakePair(sourceZip, name), qMakePair(targetZip, QString("untouched.txt"))}) {
            QProcess process;
            process.setWorkingDirectory(root.path());
            process.start(engine, {"a", pair.first, pair.second});
            QVERIFY(process.waitForFinished(30000));
            QCOMPARE(process.exitCode(), 0);
        }
        const QByteArray original = Read(sourceZip);
        ArchiveBackend sourceBackend, targetBackend;
        QString targetResult;
        connect(&targetBackend, &ArchiveBackend::finished, &targetBackend,
                [&targetResult](bool, const QString &message) { targetResult = message; });
        CApp app;
        QVERIFY(BindArchive(app.Panels[0], sourceBackend, sourceZip, name, false));
        if (archiveTarget)
            QVERIFY(BindArchive(app.Panels[1], targetBackend, targetZip, "untouched.txt", true));
        else {
            app.Panels[1].SetNewFolder(new NFsFolder::CFSFolder(destination));
            app.Panels[1].LoadFullPath();
        }
        for (unsigned i = 0; i < kNumPanelsMax; ++i) {
            app.m_PanelCallbackImp[i].Init(&app, i);
            app.Panels[i].AttachPanelCallback(&app.m_PanelCallbackImp[i]);
        }
        class Target final : public IQtDropTargetPresentation {
          public:
            int PanelAtPoint(int, int) const override { return 1; }
            bool PanelEnabled(unsigned) const override { return true; }
            int FolderAtPoint(unsigned, int, int, UString &) const override { return -1; }
            void Highlight(unsigned, int, bool) override {}
            bool IsSameDrive(const UString &, const UStringVector &) const override { return true; }
            bool ConfirmCopyToArchive(CPanel &, DWORD) override { return true; }
        } target;
        app.DropTarget().AttachPresentation(&target);
        QTableWidget list(1, 1);
        list.setSelectionBehavior(QAbstractItemView::SelectRows);
        auto *row = new QTableWidgetItem(name);
        row->setData(Qt::UserRole + 6, 0u);
        list.setItem(0, 0, row);
        list.setCurrentCell(0, 0);
        app.Panels[0].AttachSelectionView(&list);
        HRESULT entered = E_FAIL, dropped = E_FAIL;
        UInt32 flags = 0;
        QString temporary;
        QtPanelDragTransport transport(&list, [&](QDrag &drag, Qt::DropActions) {
            auto *mime = dynamic_cast<const QtPanelDragMime *>(drag.mimeData());
            if (!mime)
                return Qt::IgnoreAction;
            temporary = ToQString(mime->Data().SrcDirPrefix_Temp);
            DWORD effect = DROPEFFECT_COPY;
            entered = app.DropTarget().DragEnter(&mime->Data(), MK_CONTROL, 0, 0, &effect);
            effect = DROPEFFECT_COPY;
            dropped = app.DropTarget().Drop(&mime->Data(), MK_CONTROL, 0, 0, &effect);
            flags = mime->Data().m_Transfer.Target.Flags;
            return effect ? Qt::CopyAction : Qt::IgnoreAction;
        });
        app.Panels[0].AttachDragServices(&transport);
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &list, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            }
        });
        responder.start(1);
        app.Panels[0].OnDrag(false);
        responder.stop();
        QVERIFY2(errors.isEmpty(), qPrintable(QString("Drop HRESULT=0x%1; %2; backend=%3")
                                                  .arg(quint32(dropped), 8, 16, QLatin1Char('0'))
                                                  .arg(errors.join('\n'))
                                                  .arg(targetResult)));
        QCOMPARE(entered, S_OK);
        QCOMPARE(dropped, S_OK);
        QVERIFY(flags & (archiveTarget ? k_TargetFlags_WasProcessed
                                       : k_TargetFlags_MustBeProcessedBySource));
        QCOMPARE(Read(sourceZip), original);
        QVERIFY(!sourceBackend.busy() && !targetBackend.busy());
        QVERIFY(!row->isSelected());
        QVERIFY(!QFileInfo::exists(temporary));
        if (archiveTarget) {
            QProcess extract;
            extract.start(engine, {"x", "-y", "-o" + destination, targetZip});
            QVERIFY(extract.waitForFinished(30000));
            QCOMPARE(extract.exitCode(), 0);
            QCOMPARE(Read(QDir(destination).filePath("untouched.txt")),
                     QByteArray("untouched bytes"));
            QCOMPARE(Read(QDir(destination).filePath(name)), payload);
        }
        QCOMPARE(Read(QDir(destination).filePath(name)), payload);
    }
    void nativeArchiveDragDeliversBytesAndProtectsUnconfirmedReceiver_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("recipient-finished") << 0;
        QTest::newRow("recipient-still-using-bytes") << 1;
        QTest::newRow("drag-cancelled") << 2;
        QTest::newRow("extraction-cancelled") << 3;
        QTest::newRow("recipient-refused-drop") << 4;
        QTest::newRow("recipient-throws") << 6;
        QTest::newRow("recipient-allocation-fails") << 7;
#ifdef Q_OS_MACOS
        QTest::newRow("native-qt-ignore-unconfirmed") << 5;
#endif
    }
    void nativeArchiveDragDeliversBytesAndProtectsUnconfirmedReceiver() {
        QFETCH(int, outcome);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = "файл с пробелом.bin";
        const QByteArray payload("archive\0bytes", 13);
        QFile input(fixture.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        const QString archive = fixture.filePath("исходный архив.zip");
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        QProcess create;
        create.setWorkingDirectory(fixture.path());
        create.start(engine, {"a", "-tzip", "--", archive, name});
        QVERIFY(create.waitForFinished(10000));
        QCOMPARE(create.exitCode(), 0);
        const QByteArray original = Read(archive);
        ArchiveBackend backend;
        QList<ArchiveEntry> listed;
        connect(&backend, &ArchiveBackend::entriesReady, &backend,
                [&listed](const QList<ArchiveEntry> &entries) { listed = entries; });
        backend.list(archive);
        QTRY_VERIFY(!backend.busy());
        QCOMPARE(listed.size(), 1);
        if (outcome == 3)
            connect(&backend, &ArchiveBackend::phaseStarted, &backend,
                    [&backend] { backend.cancel(); });
        auto *resource = new QtArchiveFileResource(archive);
        CMyComPtr<IUnknown> resourceOwner = resource;
        resource->AttachExtractService(backend, "zip");
        CObjectVector<CPanelItem> items;
        CPanelItem item;
        // Actual archive metadata can be NFD when Qt creates the fixture on macOS.
        item.RelPath = listed.front().path;
        item.SizeDefined = true;
        item.Size = payload.size();
        items.Add(item);
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        QCOMPARE(agent->Init(metadata, UString("zip"), resourceOwner), S_OK);
        CMyComPtr<IFolderFolder> root;
        QCOMPARE(agent->BindToRootFolder(&root), S_OK);
        QCOMPARE(root->LoadItems(), S_OK);
        CApp app;
        app.Panels[0].SetNewFolder(root);
        QVERIFY(app.Panels[0].IsArcFolder());
        app.m_PanelCallbackImp[0].Init(&app, 0);
        app.Panels[0].AttachPanelCallback(&app.m_PanelCallbackImp[0]);
        QTableWidget list(1, 1);
        list.setSelectionBehavior(QAbstractItemView::SelectRows);
        auto *row = new QTableWidgetItem(name);
        row->setData(Qt::UserRole + 6, 0u);
        list.setItem(0, 0, row);
        app.Panels[0].AttachSelectionView(&list);
        list.setCurrentCell(0, 0);
        QString delivered, ownedRoot;
        QByteArray deliveredBytes;
        int calls = 0;
        bool claimDenied = false;
        QtPanelDragTransport::Executor executor = [&](QDrag &drag, Qt::DropActions actions) {
            ++calls;
            auto *mime = dynamic_cast<const QtPanelDragMime *>(drag.mimeData());
            if (!mime || mime->urls().size() != 1 || !(actions & Qt::CopyAction))
                return Qt::IgnoreAction;
            delivered = mime->urls().first().toLocalFile();
            deliveredBytes = Read(delivered);
            ownedRoot = ToQString(mime->Data().SrcDirPrefix_Temp);
            QtOwnedTempRegistry registry;
            for (const auto &entry : registry.Entries())
                if (QDir::cleanPath(entry.path) == QDir::cleanPath(ownedRoot))
                    claimDenied = !registry.ClaimForDeletion(entry);
            if (outcome == 0)
                mime->Data().FinishExternalReceiver();
            if (outcome == 6)
                throw std::runtime_error("recipient service failed after reading URLs");
            if (outcome == 7)
                throw std::bad_alloc();
            return outcome == 2 || outcome == 4 ? Qt::IgnoreAction : Qt::MoveAction;
        };
        if (outcome == 5)
            executor = {}; // The offscreen platform returns IgnoreAction from real QDrag::exec.
        QtPanelDragTransport transport(&list, std::move(executor));
        app.Panels[0].AttachDragServices(&transport);
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &list, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            }
        });
        responder.start(1);
        app.Panels[0].OnDrag(false);
        responder.stop();
        if (outcome == 6 || outcome == 7)
            QVERIFY(!errors.isEmpty());
        else
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QVERIFY(!transport.Active());
        QCOMPARE(app.DropTarget().SrcPanelIndex, -1);
        QCOMPARE(Read(archive), original);
        QVERIFY(!backend.busy());
        if (outcome == 3) {
            QCOMPARE(calls, 0);
            QVERIFY(row->isSelected());
            return;
        }
        if (outcome == 5) {
            QCOMPARE(calls, 0);
            auto entries = QtOwnedTempRegistry().Entries();
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const auto &entry) {
                                             return !QFileInfo::exists(
                                                 QDir(entry.path).filePath(listed.front().path));
                                         }),
                          entries.end());
            QCOMPARE(entries.size(), 1);
            ownedRoot = entries.front().path;
            delivered = QDir(ownedRoot).filePath(listed.front().path);
            deliveredBytes = Read(delivered);
            claimDenied = !QtOwnedTempRegistry().ClaimForDeletion(entries.front());
        } else
            QCOMPARE(calls, 1);
        QCOMPARE(deliveredBytes, payload);
        QVERIFY(claimDenied);
        QCOMPARE(row->isSelected(), outcome == 2 || outcome == 4 || outcome == 5 || outcome == 6 ||
                                       outcome == 7);
        if (outcome == 1 || outcome == 5 || outcome == 6 || outcome == 7) {
            QCOMPARE(Read(delivered), payload);
            QtOwnedTempRegistry registry;
            bool found = false;
            for (const auto &entry : registry.Entries()) {
                if (QDir::cleanPath(entry.path) != QDir::cleanPath(ownedRoot))
                    continue;
                found = true;
                QVERIFY(entry.externallyProtected);
                QVERIFY(!entry.active);
                QVERIFY(!registry.ClaimForDeletion(entry));
            }
            QVERIFY(found);
            // Dispose only this fixture; its protected record stays in the
            // row's private registry, never the application's registry.
            QVERIFY(QDir(ownedRoot).removeRecursively());
        } else
            QVERIFY(!QFileInfo::exists(ownedRoot));
    }
};
QTEST_MAIN(DragArchiveTests)
#include "drag_archive_tests.moc"
