#include "7zip/Common/StreamObjects.h"
#include "7zip/PropID.h"
#include "Common/StringConvert.h"
#include "Windows/PropVariant.h"
#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/opening/OpenArchive.h"
#include "archive/resources/archiveinputlayer.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchiveproperties.h"
#include "archive/resources/qtarchivereopen.h"
#include "archive/support/archive_backend_test_support.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/panels/qtpanelfolder.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <atomic>
#include <future>
#include <thread>

namespace {
class CReadBudgetInput final : public IInStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(IInStream, ISequentialInStream)
    CMyComPtr<IInStream> _input;
    bool &_released;

  public:
    std::atomic<UInt64> Remaining{1 << 16};
    bool YieldOnSeek = false;
    HRESULT ReadFailure = S_OK;
    HRESULT SeekFailure = S_OK;
    CReadBudgetInput(IInStream *input, bool &released) : _input(input), _released(released) {}
    ~CReadBudgetInput() { _released = true; }
    Z7_COM7F_IMF(Read(void *data, UInt32 size, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        if (ReadFailure != S_OK)
            return ReadFailure;
        const UInt64 remaining = Remaining.load();
        if (size && !remaining)
            return E_ABORT;
        UInt32 actual = 0;
        const HRESULT result = _input->Read(data, UInt32(qMin<UInt64>(size, remaining)), &actual);
        Remaining.fetch_sub(actual);
        if (processed)
            *processed = actual;
        return result;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        if (SeekFailure != S_OK)
            return SeekFailure;
        const HRESULT result = _input->Seek(offset, origin, position);
        if (YieldOnSeek)
            std::this_thread::yield();
        return result;
    }
};
} // namespace

class ArchiveOpeningTests : public QObject, protected ArchiveBackendTestSupport {
    Q_OBJECT
  private slots:
    void detectedMetadataOwnsNamesAndWriteReadiness_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("openType");
        QTest::addColumn<QString>("defaultName");
        QTest::addColumn<bool>("writable");
        QTest::newRow("ZIP unknown suffix") << "zip" << QStringLiteral("архив 空.unknown") << "*"
                                            << QStringLiteral("архив 空") << true;
        QTest::newRow("gzip tgz AddExt") << "gzip" << QStringLiteral("архив 空.tgz") << "*"
                                         << QStringLiteral("архив 空.tar") << true;
        QTest::newRow("gzip misleading ZIP suffix") << "gzip" << QStringLiteral("архив 空.zip")
                                                    << "*" << QStringLiteral("архив 空") << true;
        QTest::newRow("parser container")
            << "zip" << QStringLiteral("архив 空.unknown") << "#" << QString() << false;
    }
    void detectedMetadataOwnsNamesAndWriteReadiness() {
        QFETCH(QString, format);
        QFETCH(QString, name);
        QFETCH(QString, openType);
        QFETCH(QString, defaultName);
        QFETCH(bool, writable);
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY2(!engine.isEmpty(), "official 7z fixture engine is required");
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        if (!ready[0][0].toBool())
            QSKIP("Matching official module unavailable; native metadata consumer unverified");
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString input = temporary.filePath(QStringLiteral("payload 空 with spaces.bin"));
        const QByteArray payload("binary\0payload", 14);
        writeFile(input, payload);
        const QString path = temporary.filePath(name);
        QProcess creator;
        creator.start(engine, {"a", "-t" + format, "--", path, input});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray bytes = original.readAll();
        original.close();
        if (openType == "#")
            writeFile(path, bytes + bytes);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(openType.toStdWString().c_str()), UString(),
                                              nullptr),
                 S_OK);
        auto *folder = dynamic_cast<CAgentFolder *>(opening.Folder.Interface());
        QVERIFY(folder);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(folder->GetEngineResource());
        QVERIFY(resource);
        CArc arc;
        arc.Archive = folder->GetArchiveProperties();
        QCOMPARE(resource->ConfigureArc(arc), S_OK);
        QCOMPARE(QString::fromWCharArray(arc.DefaultName.Ptr()), defaultName);
        NWindows::NCOM::CPropVariant readOnly;
        QCOMPARE(folder->GetFolderProperty(kpidReadOnly, &readOnly), S_OK);
        QCOMPARE(readOnly.vt, VARTYPE(VT_BOOL));
        QCOMPARE(readOnly.boolVal != VARIANT_FALSE, !writable);
        QCOMPARE(resource->HasUpdateService(), writable);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), openType == "#" ? bytes + bytes : bytes);
    }
    void nativeMetadataUsesFileTimeAndEmptyUnsupportedProperties() {
        CObjectVector<CPanelItem> items;
        for (unsigned index = 0; index < 3; ++index) {
            CPanelItem item;
            item.RelPath = "input.bin";
            if (index < 2) {
                item.MTime.emplace();
                item.MTime->SetAsTimeFrom_Ft64_Prec(
                    UInt64(116444736000000000ULL) + 1234567 + index * 31, k_PropVar_TimePrec_100ns);
            }
            items.Add(item);
        }
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CArc arc;
        QCOMPARE(metadata.QueryInterface(IID_IInArchive, &arc.Archive), S_OK);
        CArcTime first, second;
        QCOMPARE(arc.GetItem_MTime(0, first), S_OK);
        QCOMPARE(arc.GetItem_MTime(1, second), S_OK);
        QVERIFY(first.Def);
        QVERIFY(second.Def);
        QCOMPARE(first.Prec, UInt16(k_PropVar_TimePrec_Base + 7));
        const UInt64 ticks1 = (UInt64(first.FT.dwHighDateTime) << 32) | first.FT.dwLowDateTime;
        const UInt64 ticks2 = (UInt64(second.FT.dwHighDateTime) << 32) | second.FT.dwLowDateTime;
        QCOMPARE(ticks2 - ticks1, UInt64(31));
        NWindows::NCOM::CPropVariant value;
        QCOMPARE(arc.Archive->GetProperty(2, kpidMTime, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(arc.Archive->GetProperty(0, kpidSymLink, &value), S_OK);
        QCOMPARE(value.vt, VARTYPE(VT_EMPTY));
    }
    void sourceBasicPropertiesDetectTailOffsetAndUnexpectedEnd() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString path = temp.filePath("metadata.bin");
        writeFile(path, QByteArray(64, 'x'));
        CObjectVector<CPanelItem> items;
        CMyComPtr<IFolderFolder> metadata =
            new QtPanelFolder({}, items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        auto *resourceSpec = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> resource = resourceSpec;
        resourceSpec->Info.physicalSize = 32;
        CMyComPtr2_Create<IFolderArcProps, CAgent> agent;
        QCOMPARE(agent->Init(metadata, UString("zip"), resource), S_OK);
        const CArc &arc = agent->GetArc();
        QVERIFY(arc.PhySize_Defined);
        QCOMPARE(arc.PhySize, UInt64(32));
        QVERIFY(arc.ErrorInfo.ThereIsTail);
        QCOMPARE(arc.ErrorInfo.TailSize, UInt64(32));
        QVERIFY(!agent->CanUpdate());
        resourceSpec->Info.physicalSize = 48;
        resourceSpec->Info.offset = 16;
        CArc offsetArc;
        QCOMPARE(CreateArchivePropertyTransport(metadata, &offsetArc.Archive, resource), S_OK);
        QCOMPARE(resourceSpec->ConfigureArc(offsetArc), S_OK);
        QCOMPARE(offsetArc.GetGlobalOffset(), Int64(16));
        QCOMPARE(offsetArc.AvailPhySize, UInt64(48));
        QVERIFY(!offsetArc.ErrorInfo.ThereIsTail);
        QVERIFY(!offsetArc.ErrorInfo.UnexpecedEnd);
        resourceSpec->Info.physicalSize = 128;
        resourceSpec->Info.offset = 0;
        QCOMPARE(resourceSpec->ConfigureArc(offsetArc), S_OK);
        QCOMPARE(offsetArc.GetGlobalOffset(), Int64(16));
        QVERIFY(!offsetArc.ErrorInfo.UnexpecedEnd);
        QVERIFY(!offsetArc.ErrorInfo.ThereIsTail);
        CArc truncatedArc;
        QCOMPARE(CreateArchivePropertyTransport(metadata, &truncatedArc.Archive, resource), S_OK);
        QCOMPARE(resourceSpec->ConfigureArc(truncatedArc), S_OK);
        QVERIFY(truncatedArc.ErrorInfo.UnexpecedEnd);
        QVERIFY(!truncatedArc.ErrorInfo.ThereIsTail);
        NWindows::NCOM::CPropVariant originalSize;
        QCOMPARE(arc.Archive->GetArchiveProperty(kpidPhySize, &originalSize), S_OK);
        QCOMPARE(originalSize.vt, VARTYPE(VT_UI8));
        QCOMPARE(originalSize.uhVal.QuadPart, UInt64(32));
    }
    void sharedNativeArchiveMetadata() {
        CObjectVector<CPanelItem> items;
        CPanelItem unnamed;
        unnamed.SizeDefined = true;
        unnamed.Size = 7;
        items.Add(unnamed);
        CPanelItem folder;
        folder.IsFolder = true;
        items.Add(folder);
        CMyComPtr<IFolderFolder> properties =
            new QtPanelFolder(QString(), items, QtPanelFolder::PropertyCatalog::ArchiveTransport);
        CArc arc;
        QCOMPARE(properties.QueryInterface(IID_IInArchive, &arc.Archive), HRESULT(S_OK));
        arc.DefaultName = L"content й space";
        UString name;
        QCOMPARE(arc.GetItem_DefaultPath(0, name), HRESULT(S_OK));
        QCOMPARE(QString::fromWCharArray(name.Ptr()), QStringLiteral("content й space"));
        QCOMPARE(arc.GetItem_DefaultPath(1, name), HRESULT(S_OK));
        QVERIFY(name.IsEmpty());
        bool isDir = false;
        QCOMPARE(Archive_IsItem_Dir(arc.Archive, 1, isDir), HRESULT(S_OK));
        QVERIFY(isDir);
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(arc.Archive->GetProperty(0, kpidSize, &size), HRESULT(S_OK));
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(7));
        CMyComPtr<IFolderFolder> custom = new QtPanelFolder(QString(), items);
        CMyComPtr<IInArchive> unsupported;
        QCOMPARE(custom.QueryInterface(IID_IInArchive, &unsupported), HRESULT(E_NOINTERFACE));
        QVERIFY(!unsupported);
    }
    void realArchiveAndNotArchive() {
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY2(!engine.isEmpty(), "official 7z fixture engine is required");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("имя й с пробелом.txt"));
        writeFile(source, "source data");
        const QString archive = temp.filePath(QStringLiteral("архив й space.zip"));
        QProcess creator;
        creator.start(engine, {"a", "-tzip", "--", archive, source});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend(nullptr, engine);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString("zip"), UString(), nullptr),
                 HRESULT(S_OK));
        QVERIFY(opening.Folder);
        QCOMPARE(opening.Folder->LoadItems(), HRESULT(S_OK));
        UInt32 count = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&count), HRESULT(S_OK));
        QCOMPARE(count, UInt32(1));
        NWindows::NCOM::CPropVariant name;
        QCOMPARE(opening.Folder->GetProperty(0, kpidName, &name), HRESULT(S_OK));
        QCOMPARE(QString::fromWCharArray(name.bstrVal).normalized(QString::NormalizationForm_C),
                 QFileInfo(source).fileName().normalized(QString::NormalizationForm_C));
        CFfpOpen plain(manager);
        QCOMPARE(plain.OpenFileFolderPlugin(nullptr, us2fs(source.toStdWString().c_str()),
                                            UString(), UString(), nullptr),
                 HRESULT(S_FALSE));
        QVERIFY(!plain.Folder);
        QVERIFY(!plain.Encrypted);
        QVERIFY(!backend.busy());
    }
    void passwordCallback_data() {
        QTest::addColumn<bool>("accept");
        QTest::addColumn<bool>("empty");
        QTest::newRow("accept") << true << false;
        QTest::newRow("empty") << true << true;
        QTest::newRow("cancel") << false << false;
    }
    void rangeReadFailureAndCancellationAllowRetry() {
        const QByteArray bytes("source bytes stay unchanged");
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> raw = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()),
                             size_t(bytes.size()));
        buffer->Init();
        bool released = false;
        auto *fault = new CReadBudgetInput(raw, released);
        CMyComPtr<IInStream> source = fault;
        fault->SeekFailure = E_FAIL;
        const auto failed = ArchiveInputLayer::Create(source);
        QVERIFY(!failed);
        QCOMPARE(failed.error(), E_FAIL);
        fault->SeekFailure = S_OK;
        auto opened = ArchiveInputLayer::Create(source);
        QVERIFY(opened);
        auto layer = std::move(*opened);
        CMyComPtr<IInStream> cursor;
        QCOMPARE(layer->OpenCursor(&cursor), S_OK);
        QCOMPARE(cursor->Seek(7, STREAM_SEEK_SET, nullptr), S_OK);
        char data[64];
        UInt32 count = 99;
        fault->ReadFailure = E_ABORT;
        QCOMPARE(cursor->Read(data, sizeof(data), &count), E_ABORT);
        QCOMPARE(count, UInt32(0));
        fault->ReadFailure = S_OK;
        fault->SeekFailure = E_FAIL;
        QCOMPARE(cursor->Read(data, sizeof(data), &count), E_FAIL);
        QCOMPARE(count, UInt32(0));
        fault->SeekFailure = S_OK;
        QCOMPARE(cursor->Read(data, sizeof(data), &count), S_OK);
        QCOMPARE(QByteArray(data, int(count)), bytes.mid(7));
        QCOMPARE(cursor->Read(data, sizeof(data), &count), S_OK);
        QCOMPARE(count, UInt32(0));
        QVERIFY(cursor->Seek(-1, STREAM_SEEK_SET, nullptr) != S_OK);
        QCOMPARE(cursor->Seek(0, STREAM_SEEK_SET, nullptr), S_OK);
        QCOMPARE(cursor->Read(data, sizeof(data), &count), S_OK);
        QCOMPARE(QByteArray(data, int(count)), bytes);
    }
    void nativeStreamOpeningSkipsPayloadAndRetainsReaders() {
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        ArchiveBackend backend(nullptr, engine);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog());
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(ready[0][0].toBool());
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(QDir().mkdir(fixture.filePath("inside")));
        QByteArray payload(1 << 19, '\0');
        for (int i = 0; i < payload.size(); ++i)
            payload[i] = char(i * 13);
        writeFile(fixture.filePath("inside/file.bin"), payload);
        const QString archive = fixture.filePath("input.tar");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(engine, {"a", "-ttar", "--", archive, "inside"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile file(archive);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> raw = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()),
                             size_t(bytes.size()));
        buffer->Init();
        bool released = false;
        auto *budget = new CReadBudgetInput(raw, released);
        CMyComPtr<IInStream> input = budget;
        raw.Release();
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        opening.OwnedStreamFile = true;
        const QString virtualPath = fixture.filePath("virtual archive.tar");
        // A full intermediate spool exceeds the source's opening read budget.
        QCOMPARE(opening.OpenFileFolderPlugin(input, us2fs(virtualPath.toStdWString().c_str()),
                                              UString(L"tar"), UString(), nullptr),
                 S_OK);
        budget->Remaining = UInt64(-1);
        CMyComPtr<IFolderFolder> child;
        QCOMPARE(opening.Folder->BindToFolder(L"inside", &child), S_OK);
        QVERIFY(child);
        CMyComPtr<IInArchiveGetStream> streams;
        QCOMPARE(child.QueryInterface(IID_IInArchiveGetStream, &streams), S_OK);
        CMyComPtr<ISequentialInStream> first, second;
        QCOMPARE(streams->GetStream(0, &first), S_OK);
        QVERIFY(first);
        auto *agent = dynamic_cast<CAgentFolder *>(child.Interface());
        QVERIFY(agent);
        CMyComPtr<IFolderFolder> refreshed;
        QCOMPARE(ReOpenArchiveFolder(*agent, {}, nullptr, &refreshed, false, UString()), S_OK);
        CMyComPtr<IInArchiveGetStream> refreshedStreams;
        QCOMPARE(refreshed.QueryInterface(IID_IInArchiveGetStream, &refreshedStreams), S_OK);
        QCOMPARE(refreshedStreams->GetStream(0, &second), S_OK);
        QVERIFY(second);
        auto *resource = dynamic_cast<QtArchiveFileResource *>(agent->GetEngineResource());
        QVERIFY(resource);
        QCOMPARE(resource->CloseNativeOpen(), S_OK);
        opening.Folder.Release();
        child.Release();
        streams.Release();
        refreshedStreams.Release();
        refreshed.Release();
        input.Release();
        QVERIFY(!released);
        CMyComPtr<IInStream> seekable;
        QCOMPARE(second.QueryInterface(IID_IInStream, &seekable), S_OK);
        QCOMPARE(seekable->Seek(12345, STREAM_SEEK_SET, nullptr), S_OK);
        char range[97];
        UInt32 actual = 0;
        QCOMPARE(second->Read(range, sizeof(range), &actual), S_OK);
        QCOMPARE(QByteArray(range, int(actual)), payload.mid(12345, sizeof(range)));
        QCOMPARE(first->Read(range, sizeof(range), &actual), S_OK);
        QCOMPARE(QByteArray(range, int(actual)), payload.left(sizeof(range)));
        CMyComPtr<IInStream> oldCursor;
        QCOMPARE(first.QueryInterface(IID_IInStream, &oldCursor), S_OK);
        budget->YieldOnSeek = true;
        const auto readRanges = [&payload](IInStream *stream, int seed) {
            for (int i = 0; i < 500; ++i) {
                const int offset = (i * 357 + seed) % (payload.size() - 97);
                char data[97];
                UInt32 count = 0;
                if (stream->Seek(offset, STREAM_SEEK_SET, nullptr) != S_OK ||
                    stream->Read(data, sizeof(data), &count) != S_OK ||
                    QByteArray(data, int(count)) != payload.mid(offset, sizeof(data)))
                    return false;
            }
            return true;
        };
        auto oldReads = std::async(std::launch::async, readRanges, oldCursor.Interface(), 17);
        auto newReads = std::async(std::launch::async, readRanges, seekable.Interface(), 12345);
        QVERIFY(oldReads.get());
        QVERIFY(newReads.get());
        oldCursor.Release();
        seekable.Release();
        second.Release();
        first.Release();
        QVERIFY(released);
    }
    void memoryStreamRetainsBoundFolderAfterCallerRelease() {
        const QString engine = QStandardPaths::findExecutable("7z");
        QVERIFY2(!engine.isEmpty(), "official 7z fixture engine is required");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(QDir().mkdir(temp.filePath("inside")));
        writeFile(temp.filePath("inside/file.txt"), "payload");
        const QString archive = temp.filePath("inner.zip");
        QProcess creator;
        creator.setWorkingDirectory(temp.path());
        creator.start(engine, {"a", "-tzip", "--", archive, "inside"});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile file(archive);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        auto *buffer = new CBufferInStream;
        CMyComPtr<IInStream> stream = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(bytes.constData()),
                             size_t(bytes.size()));
        buffer->Init();
        ArchiveBackend backend(nullptr, engine);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(stream, FString("virtual-name.zip"), UString("zip"),
                                              UString(), nullptr),
                 HRESULT(S_OK));
        CMyComPtr<IFolderFolder> child;
        QCOMPARE(opening.Folder->BindToFolder(L"inside", &child), HRESULT(S_OK));
        QVERIFY(child);
        opening.Folder.Release();
        stream.Release();
        QCOMPARE(child->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(child->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        NWindows::NCOM::CPropVariant name;
        QCOMPARE(child->GetProperty(0, kpidName, &name), S_OK);
        QCOMPARE(QString::fromWCharArray(name.bstrVal), QString("file.txt"));
        child.Release();
        const QByteArray invalid("not an archive");
        buffer = new CBufferInStream;
        stream = buffer;
        buffer->Buf.CopyFrom(reinterpret_cast<const Byte *>(invalid.constData()),
                             size_t(invalid.size()));
        buffer->Init();
        CFfpOpen bad(manager);
        QCOMPARE(
            bad.OpenFileFolderPlugin(stream, FString("bad.zip"), UString(), UString(), nullptr),
            HRESULT(S_FALSE));
        QVERIFY(!bad.Folder);
        QVERIFY(!backend.busy());
    }
    void passwordCallback() {
        QFETCH(bool, accept);
        QFETCH(bool, empty);
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath("fixture.7z");
        QVERIFY(NativeArchiveFixture::Write(
            archive, {{QStringLiteral("папка/имя с пробелом.txt"), "abc"}}, "7z",
            empty ? QStringLiteral("") : QStringLiteral("fixture пароль")));
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        bool observed = false;
        QTimer responder;
        responder.setInterval(5);
        connect(&responder, &QTimer::timeout, this, [&] {
            auto *dialog = dynamic_cast<CPasswordDialog *>(QApplication::activeModalWidget());
            if (!dialog || observed)
                return;
            observed = true;
            dialog->findChild<QLineEdit *>("passwordEdit")
                ->setText(empty ? QString() : QStringLiteral("fixture пароль"));
            if (accept)
                dialog->accept();
            else
                dialog->reject();
        });
        responder.start();
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(), UString(), nullptr),
                 HRESULT(accept ? S_OK : E_ABORT));
        QVERIFY(observed);
        QCOMPARE(bool(opening.Folder), accept);
        if (accept) {
            QVERIFY(opening.Encrypted);
            QCOMPARE(QString::fromWCharArray(opening.Password.Ptr()),
                     empty ? QString() : QStringLiteral("fixture пароль"));
        }
        QVERIFY(!backend.busy());
    }
    void failedStartIsNotNotArchive() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString archive = temp.filePath("fixture.7z");
        writeFile(archive, "fixture transport input");
        ArchiveBackend backend(nullptr, "/nonexistent/sevenzip-opening-fixture");
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(archive.toStdWString().c_str()),
                                              UString(), UString(), nullptr),
                 HRESULT(E_FAIL));
        QVERIFY(!opening.Folder);
        QVERIFY(!backend.busy());
    }
};

QTEST_MAIN(ArchiveOpeningTests)
#include "archive_opening_tests.moc"
