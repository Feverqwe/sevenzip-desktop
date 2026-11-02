#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/native_archive_fixture.h"
#include "filemanager/UpdateCallback100.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include <QStandardPaths>

namespace {
class CPublicationFailure final : public IFolderArchiveUpdateCallback,
                                  public IFolderArchiveUpdateCallback_MoveArc,
                                  public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IFolderArchiveUpdateCallback, IFolderArchiveUpdateCallback_MoveArc)
    CMyComPtr<IFolderArchiveUpdateCallback> callback_;

  public:
    bool Reached = false;
    explicit CPublicationFailure(IFolderArchiveUpdateCallback *callback) : callback_(callback) {}
    Z7_COM7F_IMF(SetTotal(UInt64 value)) override { return callback_->SetTotal(value); }
    Z7_COM7F_IMF(SetCompleted(const UInt64 *value)) override {
        return callback_->SetCompleted(value);
    }
    Z7_COM7F_IMF(CompressOperation(const wchar_t *name)) override {
        return callback_->CompressOperation(name);
    }
    Z7_COM7F_IMF(DeleteOperation(const wchar_t *name)) override {
        return callback_->DeleteOperation(name);
    }
    Z7_COM7F_IMF(OperationResult(Int32 result)) override {
        return callback_->OperationResult(result);
    }
    Z7_COM7F_IMF(UpdateErrorMessage(const wchar_t *message)) override {
        return callback_->UpdateErrorMessage(message);
    }
    Z7_COM7F_IMF(SetNumFiles(UInt64 count)) override { return callback_->SetNumFiles(count); }
    Z7_COM7F_IMF(MoveArc_Start(const wchar_t *, const wchar_t *, UInt64, Int32)) override {
        return S_OK;
    }
    Z7_COM7F_IMF(MoveArc_Progress(UInt64, UInt64)) override { return S_OK; }
    Z7_COM7F_IMF(MoveArc_Finish()) override {
        Reached = true;
        return E_FAIL;
    }
    Z7_COM7F_IMF(Before_ArcReopen()) override { return S_OK; }
};

class CDeletionRetryWorker final : public CProgressThreadVirt {
    CMyComPtr<IFolderFolder> folder_;
    QString source_;
    bool rename_;
    HRESULT ProcessVirt() override {
        CMyComPtr<IFolderOperations> operations;
        RINOK(folder_.QueryInterface(IID_IFolderOperations, &operations))
        CMyComPtr2_Create<IFolderArchiveUpdateCallback, CUpdateCallback100Imp> ui;
        ui->ProgressDialog = this;
        ui->Init();
        auto *faultSpec = new CPublicationFailure(ui);
        CMyComPtr<IFolderArchiveUpdateCallback> fault = faultSpec;
        const std::wstring parent = QFileInfo(source_).absolutePath().toStdWString();
        const std::wstring name = QFileInfo(source_).fileName().toStdWString();
        const wchar_t *names[] = {name.c_str()};
        AddResult = operations->CopyFrom(0, parent.c_str(), names, 1, fault);
        PublicationReached = faultSpec->Reached;
        if (AddResult != E_FAIL || !PublicationReached)
            return E_FAIL;
        if (!rename_)
            return operations->CreateFolder(L"new folder 空", ui);
        RINOK(folder_->LoadItems())
        UInt32 count = 0;
        RINOK(folder_->GetNumberOfItems(&count))
        for (UInt32 i = 0; i < count; ++i) {
            NWindows::NCOM::CPropVariant value;
            RINOK(folder_->GetProperty(i, kpidName, &value))
            if (value.vt == VT_BSTR && QString::fromWCharArray(value.bstrVal) == "keep.txt")
                return operations->Rename(i, L"renamed 空.txt", ui);
        }
        return E_FAIL;
    }

  public:
    HRESULT AddResult = S_OK;
    bool PublicationReached = false;
    CDeletionRetryWorker(IFolderFolder *folder, QString source, bool rename)
        : folder_(folder), source_(std::move(source)), rename_(rename) {
        DisableUserQuestions = true;
    }
};
} // namespace

class ArchiveInputDeletionTests final : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
  private slots:
    void failedAddDoesNotDeleteInputsDuringNextMutation_data() {
        QTest::addColumn<bool>("rename");
        QTest::newRow("rename") << true;
        QTest::newRow("create-folder") << false;
    }
    void failedAddDoesNotDeleteInputsDuringNextMutation() {
        QFETCH(bool, rename);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath("archive 空.zip");
        const QString source = fixture.filePath("source 空.txt");
        writeFile(source, "retained source bytes");
        NativeArchiveFixture::Item kept;
        kept.Path = "keep.txt";
        kept.Bytes = "original bytes";
        QVERIFY(NativeArchiveFixture::Write(archive, {kept}, "zip"));
        ArchiveBackend backend(nullptr, QStandardPaths::findExecutable("7z"));
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(), nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        CompressionOptions options;
        options.DeleteAfterCompressing = true;
        resource->SetCompressionOptions(options);
        CDeletionRetryWorker worker(opening.Folder, source, rename);
        auto transport = resource->CreateProgressTransport(worker);
        QCOMPARE(worker.Create(L"Retry after failed Add", nullptr), S_OK);
        QVERIFY(worker.PublicationReached);
        QCOMPARE(worker.AddResult, E_FAIL);
        QCOMPARE(worker.Result, S_OK);
        QFile original(source);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("retained source bytes"));
    }
};
QTEST_MAIN(ArchiveInputDeletionTests)
#include "archive_input_deletion_tests.moc"
