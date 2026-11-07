#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/compress/CompressDialog.h"
#include "gui/UpdateCallbackGUI.h"
#include "gui/UpdateGUI.h"
#include "operations/CompressCall.h"
#include "platform/shell/qtshellarchiveservices.h"
#include "support/progress_dialog.h"
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

namespace {
UString us(const QString &s) { return UString(s.toStdWString().c_str()); }
class CModalServices final : public IQtUpdateGUIServices {
  public:
    HRESULT DialogResult = S_OK;
    unsigned Calls = 0;
    NCompressDialog::CInfo Seen;
    std::function<void(CCompressDialog &)> Edit;
    HRESULT PresentCompressDialog(CCompressDialog &dialog,
                                  const CObjectVector<NWildcard::CCensorPath> &,
                                  QWidget *) override {
        Calls++;
        Seen = dialog.Info;
        if (Edit)
            Edit(dialog);
        return DialogResult;
    }
    HRESULT UpdateArchive(CCodecs *, const CObjectVector<COpenType> &, const UString &,
                          NWildcard::CCensor &, CUpdateOptions &, CUpdateErrorInfo &,
                          IOpenCallbackUI *, IUpdateCallbackUI2 *, bool) override {
        return E_FAIL;
    }
};
class CDismissal {
    QTimer _timer;

  public:
    bool Cancel = false;
    bool Canceled = false;
    QStringList Errors;
    CDismissal() {
        QObject::connect(&_timer, &QTimer::timeout, [this] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (box->standardButtons().testFlag(QMessageBox::Yes))
                    box->done(QMessageBox::Yes);
                else {
                    Errors.append(box->text());
                    box->accept();
                }
                return;
            }
            auto *progress = dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget());
            if (!progress)
                return;
            auto *button = progress->findChild<QPushButton *>("IDCANCEL");
            if (TestProgressDialog::ResultWindow(progress)) {
                const QString detail = TestProgressDialog::ReportText(*progress);
                if (!detail.isEmpty())
                    Errors.append(detail);
                TestProgressDialog::CloseResult(*progress);
            } else if (Cancel && !Canceled && button) {
                Canceled = true;
                // The timer callback enters a nested modal loop. A separate
                // queued callback answers it without reentering this timer.
                QTimer::singleShot(0, [] {
                    if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                        if (auto *yes = box->button(QMessageBox::Yes))
                            yes->click();
                });
                button->click();
            }
        });
        _timer.start(5);
    }
};
} // namespace

class ShellUpdateGuiTests final : public QObject {
    Q_OBJECT
    QString _module;
    quint32 _version = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official native runtime unavailable");
#else
        _module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        _version = probe.version();
#endif
    }
    void concreteSourceCompressWorker_data() {
        QTest::addColumn<QString>("type");
        QTest::newRow("7z") << QString("7z");
        QTest::newRow("zip") << QString("zip");
        QTest::newRow("hash") << QString("hash");
    }
    void sourceModalOptionsRoundTrip() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空 with spaces.bin"));
        QVERIFY(SourceExtractFixture::writeFile(path, "abc"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        QtShellArchiveServices service(backend, nullptr);
        CCodecs &codecs = service.Codecs();
        CObjectVector<COpenType> types;
        QVERIFY(ParseOpenTypes(codecs, UString(L"7z"), types));
        CUpdateOptions options;
        options.SetActionCommand_Add();
        const UString target = us(fixture.filePath("before.7z"));
        QVERIFY(options.InitFormatIndex(&codecs, types, target));
        QVERIFY(options.SetArcPath(&codecs, target));
        options.Commands.Front().ActionSet = NUpdateArchive::k_ActionSet_Fresh;
        options.SetArcMTime = options.PreserveATime = true;
        auto &property = options.MethodMode.Properties.AddNew();
        property.Name = "tc-";
        NWildcard::CCensor censor;
        censor.AddPreItem_NoWildcard(us(path));
        CUpdateCallbackGUI callback;
        callback.PasswordIsDefined = true;
        callback.Password = "before";
        CModalServices modal;
        const int zip = backend.codecCatalog()->formatIndex("zip");
        QVERIFY(zip >= 0);
        const UString editedPath = us(fixture.filePath("edited 空.zip"));
        modal.Edit = [&](CCompressDialog &dialog) {
            auto &di = dialog.Info;
            di.FormatIndex = zip;
            di.ArcPath = editedPath;
            di.Password = QStringLiteral("после 空");
            di.UpdateMode = NCompressDialog::NUpdateMode::kSync;
            di.PathMode = NWildcard::k_AbsPath;
            di.OpenShareForWrite = di.DeleteAfterCompressing = true;
            di.SFXMode = true;
            di.SymLinks.SetTrueTrue();
            di.PreserveATime.Def = true;
            di.PreserveATime.Val = false;
            di.VolumeSizes.Add(2 << 20);
            di.Options = "-mm=Copy";
        };
        QCOMPARE(ShowDialog(&codecs, censor.CensorPaths, options, &callback, nullptr, modal), S_OK);
        QCOMPARE(modal.Calls, 1u);
        QCOMPARE(modal.Seen.UpdateMode, NCompressDialog::NUpdateMode::kFresh);
        QCOMPARE(modal.Seen.Password, QStringLiteral("before"));
        QVERIFY(!modal.Seen.KeepName);
        QVERIFY(modal.Seen.CTime.Def && !modal.Seen.CTime.Val);
        QVERIFY(modal.Seen.SetArcMTime.Val && modal.Seen.PreserveATime.Val);
        QVERIFY(options.Commands.Front().ActionSet.IsEqualTo(NUpdateArchive::k_ActionSet_Sync));
        QCOMPARE(options.PathMode, NWildcard::k_AbsPath);
        QCOMPARE(options.MethodMode.Type.FormatIndex, zip);
        QVERIFY(options.MethodMode.Type_Defined);
        QVERIFY(options.OpenShareForWrite && !options.DeleteAfterCompressing &&
                options.SymLinks.Val);
        QVERIFY(!options.SfxMode && options.SfxModule.IsEmpty());
        QVERIFY(!options.PreserveATime);
        QCOMPARE(options.VolumesSizes.Size(), 1u);
        QCOMPARE(options.VolumesSizes[0], UInt64(2 << 20));
        QCOMPARE(QString::fromWCharArray(options.ArchivePath.GetFinalPath().Ptr()),
                 QString::fromWCharArray(editedPath.Ptr()));
        QVERIFY(callback.PasswordIsDefined);
        QCOMPARE(QString::fromWCharArray(callback.Password.Ptr()), QStringLiteral("после 空"));
        bool method = false, creationTime = false;
        FOR_VECTOR(i, options.MethodMode.Properties) {
            const auto &p = options.MethodMode.Properties[i];
            if (p.Name == UString(L"m") && p.Value == UString(L"Copy"))
                method = true;
            if (p.Name == UString(L"tc") && p.Value == UString(L"off"))
                creationTime = true;
        }
        QVERIFY(method && creationTime);
        QVERIFY(!QFileInfo::exists(QString::fromWCharArray(editedPath.Ptr())));
    }
    void sourceModalCancelDoesNotPublishEdits() {
        CCodecs codecs;
        codecs.Formats = ArchiveCodecCatalog(_module, _version).nativeCodecs().Formats;
        CObjectVector<COpenType> types;
        QVERIFY(ParseOpenTypes(codecs, UString(L"7z"), types));
        CUpdateOptions options;
        options.SetActionCommand_Add();
        QVERIFY(options.InitFormatIndex(&codecs, types, UString(L"before.7z")));
        QVERIFY(options.SetArcPath(&codecs, UString(L"before.7z")));
        CUpdateCallbackGUI callback;
        CModalServices modal;
        modal.DialogResult = E_ABORT;
        modal.Edit = [](CCompressDialog &dialog) {
            dialog.Info.DeleteAfterCompressing = true;
            dialog.Info.Password = QStringLiteral("discard");
            dialog.Info.ArcPath = "discard.7z";
        };
        QCOMPARE(ShowDialog(&codecs, {}, options, &callback, nullptr, modal), E_ABORT);
        QCOMPARE(modal.Calls, 1u);
        QVERIFY(!options.DeleteAfterCompressing && !callback.PasswordIsDefined);
        QCOMPARE(options.ArchivePath.GetFinalPath(), UString(L"before.7z"));
        options.Commands.AddNew();
        bool refused = false;
        try {
            ShowDialog(&codecs, {}, options, &callback, nullptr, modal);
        } catch (const char *) {
            refused = true;
        }
        QVERIFY(refused);
        QCOMPARE(modal.Calls, 1u);
        options.Commands.DeleteBack();
        options.Commands.Front().ActionSet = NUpdateArchive::k_ActionSet_Delete;
        QCOMPARE(ShowDialog(&codecs, {}, options, &callback, nullptr, modal), E_NOTIMPL);
        QCOMPARE(modal.Calls, 1u);
    }
    void externalMethodsUseSourceEncoderFilterRules() {
        CObjectVector<CCodecInfoUser> codecs;
        const auto add = [&](const char *name, bool encoder, bool filter, UInt32 streams) {
            auto &codec = codecs.AddNew();
            codec.Name = name;
            codec.EncoderIsAssigned = encoder;
            codec.IsFilter_Assigned = true;
            codec.IsFilter = filter;
            codec.NumStreams = streams;
        };
        add("LZMA2", true, false, 1);
        add("Custom Encoder", true, false, 1);
        add("Decoder", false, false, 1);
        add("Filter", true, true, 1);
        add("MultiStream", true, false, 2);
        CCompressDialog dialog;
        dialog.ExternalMethods.Add(AString("old"));
        dialog.SetMethods(codecs);
        QCOMPARE(dialog.ExternalMethods.Size(), 1u);
        QCOMPARE(dialog.ExternalMethods[0], AString("Custom Encoder"));
    }
    void concreteSourceCompressWorker() {
        QFETCH(QString, type);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), "abc"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        QtShellArchiveServices service(backend, nullptr);
        UStringVector paths;
        paths.Add(us(fixture.filePath(name)));
        const QString target = "result." + (type == "hash" ? "sha256" : type);
        CDismissal dismissal;
        service.CompressFiles(us(fixture.path() + '/'), us(target), us(type), false, paths, false,
                              false, false);
        QCOMPARE(service.Result(), S_OK);
        QVERIFY(dismissal.Errors.isEmpty());
        QProcess verify;
        verify.setWorkingDirectory(fixture.path());
        verify.start(QStandardPaths::findExecutable("7z"), {"t", "--", target});
        QVERIFY(verify.waitForFinished(10000));
        QCOMPARE(verify.exitCode(), 0);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(name)), QByteArray("abc"));
        QVERIFY(!backend.busy());
    }
    void brokenArchiveReportsOneSourceErrorAndRetainsBytes() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空.bin"));
        QVERIFY(SourceExtractFixture::writeFile(path, "abc"));
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath("result.7z"), "sentinel"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        QtShellArchiveServices service(backend, nullptr);
        UStringVector paths;
        paths.Add(us(path));
        CDismissal dismissal;
        service.CompressFiles(us(fixture.path() + '/'), UString(L"result.7z"), UString(L"7z"),
                              false, paths, false, false, false);
        QVERIFY(service.Result() != S_OK);
        QCOMPARE(dismissal.Errors.size(), 1);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath("result.7z")),
                 QByteArray("sentinel"));
        QCOMPARE(SourceExtractFixture::readFile(path), QByteArray("abc"));
        QVERIFY(!backend.busy());
    }
    void actualProgressCancelRollsBackOutput() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空 large.bin"));
        QByteArray data(32 * 1024 * 1024, Qt::Uninitialized);
        quint32 state = 0x91a0beef;
        for (char &byte : data) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            byte = char(state >> 24);
        }
        QVERIFY(SourceExtractFixture::writeFile(path, data));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        QtShellArchiveServices service(backend, nullptr);
        UStringVector paths;
        paths.Add(us(path));
        CDismissal dismissal;
        dismissal.Cancel = true;
        service.CompressFiles(us(fixture.path() + '/'), UString(L"result.7z"), UString(L"7z"),
                              false, paths, false, false, false);
        QVERIFY(dismissal.Canceled);
        QCOMPARE(service.Result(), E_ABORT);
        QVERIFY(dismissal.Errors.isEmpty());
        QVERIFY(!QFileInfo::exists(fixture.filePath("result.7z")));
        QCOMPARE(SourceExtractFixture::readFile(path), data);
        QVERIFY(!backend.busy());
    }
    void replacedCatalogRefusesStaleServiceWithoutWriting() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("данные 空.bin"));
        QVERIFY(SourceExtractFixture::writeFile(path, "abc"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        QtShellArchiveServices stale(backend, nullptr);
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        UStringVector paths;
        paths.Add(us(path));
        stale.CompressFiles(us(fixture.path() + '/'), UString(L"stale.7z"), UString(L"7z"), false,
                            paths, false, false, false);
        QCOMPARE(stale.Result(), E_ABORT);
        QVERIFY(!QFileInfo::exists(fixture.filePath("stale.7z")));
        QCOMPARE(SourceExtractFixture::readFile(path), QByteArray("abc"));
        QVERIFY(!backend.busy());
    }
};
QTEST_MAIN(ShellUpdateGuiTests)
#include "shell_update_gui_tests.moc"
