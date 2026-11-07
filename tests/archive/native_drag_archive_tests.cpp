#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivefileresource.h"
#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/progress_dialog.h"
#include <QFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray Read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool BindArchive(CPanel &panel, ArchiveBackend &backend, const QString &path, const QString &name) {
    auto *resource = new QtArchiveFileResource(path);
    CMyComPtr<IUnknown> owner = resource;
    resource->AttachExtractService(backend, "zip");
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
class Receiver final : public IQtPanelDragServices {
  public:
    int outcome = 0;
    QString root;
    QByteArray payload;
    bool claimDenied = false;
    HRESULT namesResult = E_FAIL;
    CMyComPtr<IDataObject> retained;
    HRESULT DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                       DWORD &effect) override {
        RINOK(CreatePanelDragDataObject(data.shared_from_this(), &retained))
        root = ToQString(data.SrcDirPrefix_Temp);
        if (!data.BeginExternalReceiver())
            return E_FAIL;
        source.GiveFeedback(allowed & DROPEFFECT_COPY);
        const HRESULT result = source.QueryContinueDrag(outcome == 2, 0);
        if (result == DRAGDROP_S_CANCEL) {
            data.FinishExternalReceiver();
            return result;
        }
        UStringVector names;
        namesResult = ReadPanelDragDataObject(retained)->GetNames(names);
        if (namesResult == S_OK && names.Size() == 1)
            payload = Read(ToQString(names[0]));
        QtOwnedTempRegistry registry;
        for (const auto &entry : registry.Entries())
            if (QDir::cleanPath(entry.path) == QDir::cleanPath(root))
                claimDenied = !registry.ClaimForDeletion(entry);
        if (outcome == 0 || namesResult != S_OK)
            data.FinishExternalReceiver();
        effect = namesResult == S_OK ? DWORD(DROPEFFECT_COPY) : DWORD(DROPEFFECT_NONE);
        return result;
    }
};
} // namespace

class NativeDragArchiveTests final : public QObject {
    Q_OBJECT
  private slots:
    void deferredNativeDataRequestUsesArchiveOwner_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("receiver-finished") << 0;
        QTest::newRow("retained-receiver") << 1;
        QTest::newRow("escape-before-data") << 2;
        QTest::newRow("cancel-extraction") << 3;
    }
    void deferredNativeDataRequestUsesArchiveOwner() {
        QFETCH(int, outcome);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray previous = qgetenv("SEVENZIP_OWNED_TEMP_REGISTRY");
        qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", fixture.filePath("registry.json").toUtf8());
        struct Restore {
            QByteArray previous;
            ~Restore() { qputenv("SEVENZIP_OWNED_TEMP_REGISTRY", previous); }
        } restore{previous};
        const QString name = QStringLiteral("archive перенос δ with spaces.bin");
        const QByteArray payload("native\0archive", 14);
        QFile input(fixture.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write(payload), qint64(payload.size()));
        input.close();
        const QString archive = fixture.filePath("source.zip");
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        QProcess create;
        create.setWorkingDirectory(fixture.path());
        create.start(engine, {"a", "-tzip", "--", archive, name});
        QVERIFY(create.waitForFinished(30000));
        QCOMPARE(create.exitCode(), 0);
        const QByteArray original = Read(archive);
        ArchiveBackend backend;
        CApp app;
        QVERIFY(BindArchive(app.Panels[0], backend, archive, name));
        app.m_PanelCallbackImp[0].Init(&app, 0);
        app.Panels[0].AttachPanelCallback(&app.m_PanelCallbackImp[0]);
        QTableWidget list(1, 1);
        list.setSelectionBehavior(QAbstractItemView::SelectRows);
        auto *row = new QTableWidgetItem(name);
        row->setData(Qt::UserRole + 6, 0u);
        list.setItem(0, 0, row);
        list.setCurrentCell(0, 0);
        app.Panels[0].AttachSelectionView(&list);
        Receiver receiver;
        receiver.outcome = outcome;
        app.Panels[0].AttachDragServices(&receiver);
        int starts = 0;
        connect(&backend, &ArchiveBackend::phaseStarted, &list, [&] {
            ++starts;
            if (outcome == 3)
                backend.cancel();
        });
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &list, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors.append(box->text());
                box->accept();
            } else if (auto *result =
                           qobject_cast<QtTaskResultDialog *>(QApplication::activeModalWidget())) {
                errors.append(
                    result->findChild<QPlainTextEdit *>("taskResultReport")->toPlainText());
                result->reject();
            } else if (auto *progress =
                           TestProgressDialog::ResultWindow(QApplication::activeModalWidget())) {
                const QString report = TestProgressDialog::ReportText(*progress);
                if (!report.isEmpty())
                    errors.append(report);
                TestProgressDialog::CloseResult(*progress);
            }
        });
        responder.start(1);
        app.Panels[0].OnDrag(false);
        responder.stop();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
        QCOMPARE(Read(archive), original);
        QCOMPARE(Read(input.fileName()), payload);
        QVERIFY(!backend.busy());
        QCOMPARE(row->isSelected(), outcome == 2 || outcome == 3);
        QCOMPARE(app.DropTarget().SrcPanelIndex, -1);
        if (outcome == 2) {
            QCOMPARE(starts, 0);
        } else if (outcome == 3) {
            QVERIFY(starts > 0);
            QVERIFY(receiver.namesResult != S_OK);
            QVERIFY(receiver.payload.isEmpty());
        } else {
            QVERIFY(starts > 0);
            QCOMPARE(receiver.namesResult, S_OK);
            QCOMPARE(receiver.payload, payload);
            QVERIFY(receiver.claimDenied);
        }
        if (outcome == 1) {
            UStringVector names;
            QCOMPARE(ReadPanelDragDataObject(receiver.retained)->GetNames(names), S_OK);
            QCOMPARE(names.Size(), unsigned(1));
            QCOMPARE(Read(ToQString(names[0])), payload);
            QVERIFY(QDir(receiver.root).removeRecursively()); // Only the disposable owned fixture.
        } else {
            QVERIFY(!QFileInfo::exists(receiver.root));
        }
    }
};
QTEST_MAIN(NativeDragArchiveTests)
#include "native_drag_archive_tests.moc"
