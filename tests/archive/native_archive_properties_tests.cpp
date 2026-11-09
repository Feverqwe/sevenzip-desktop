#include "Windows/PropVariant.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/PropertyName.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/support/property_projection_support.h"
#include "settings/ViewSettings.h"
#include "ui/support/layout_inspection.h"
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

namespace {
class COpenUi final : public IOpenCallbackUI {
  public:
    HRESULT Open_CheckBreak() override { return S_OK; }
    HRESULT Open_SetTotal(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_SetCompleted(const UInt64 *, const UInt64 *) override { return S_OK; }
    HRESULT Open_Finished() override { return S_OK; }
    HRESULT Open_CryptoGetTextPassword(BSTR *) override { return E_ABORT; }
};
} // namespace

class NativeArchivePropertiesTests final : public QObject {
    Q_OBJECT
    QString engine_, module_;
    quint32 version_ = 0;
    bool create(const QString &format, const QString &path, const QString &input) {
        QProcess creator;
        creator.start(engine_, {"a", "-t" + format, "--", path, input});
        return creator.waitForFinished(10000) && creator.exitCode() == 0;
    }
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official module unavailable; native archive properties unverified");
#else
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        engine_ = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine_.isEmpty());
        ArchiveCodecCatalog probe(module_, 0);
        QCOMPARE(probe.result(), S_OK);
        version_ = probe.version();
#endif
    }
    void wrongExtensionRetainsNativeDiagnostics_data() {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("extension");
        QTest::newRow("ZIP named 7z") << "zip" << "7z";
        QTest::newRow("7z named ZIP") << "7z" << "zip";
    }
    void wrongExtensionRetainsNativeDiagnostics() {
        QFETCH(QString, format);
        QFETCH(QString, extension);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("исходный 空 файл.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("native diagnostics"), qint64(18));
        input.close();
        const QString path = fixture.filePath(QStringLiteral("архив 空 with spaces.") + extension);
        QProcess creator;
        creator.start(engine_, {"a", "-t" + format, "--", path, input.fileName()});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        QFile archive(path);
        QVERIFY(archive.open(QIODevice::ReadOnly));
        const QByteArray original = archive.readAll();
        archive.close();

        // Opening and listing must use the retained official reader even when
        // the separate command-line executable is unavailable.
        ArchiveBackend backend(nullptr, fixture.filePath("missing 7z executable"));
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        auto catalog = backend.codecCatalog();
        std::weak_ptr<const ArchiveCodecCatalog> retained = catalog;
        COpenUi callback;
        CArchiveLink native;
        QCOMPARE(catalog->openArchive(native, path, QString(), &callback), S_OK);
        QVERIFY(!native.Arcs.IsEmpty());
        const CArc &arc = *native.GetArc();
        QVERIFY(arc.FormatIndex >= 0);
        QVERIFY(arc.ErrorInfo.ErrorFormatIndex >= 0);
        const QString nativeType = ToQString(catalog->nativeCodecs().Formats[arc.FormatIndex].Name);
        const QString rejectedType =
            ToQString(catalog->nativeCodecs().Formats[arc.ErrorInfo.ErrorFormatIndex].Name);
        QCOMPARE(nativeType.toLower(), format);
        QCOMPARE(rejectedType.toLower(), extension);

        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(ToUString(path)), UString(), UString(),
                                              nullptr),
                 S_OK);
        QCOMPARE(opening.Folder->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(opening.Folder->GetNumberOfItems(&count), S_OK);
        QCOMPARE(count, UInt32(1));
        NWindows::NCOM::CPropVariant name, size;
        QCOMPARE(opening.Folder->GetProperty(0, kpidName, &name), S_OK);
        QCOMPARE(name.vt, VARTYPE(VT_BSTR));
        // The official producer enumerates macOS filesystem names in NFD.
        QCOMPARE(ToQString(name.bstrVal).normalized(QString::NormalizationForm_C),
                 QFileInfo(input.fileName()).fileName().normalized(QString::NormalizationForm_C));
        QCOMPARE(opening.Folder->GetProperty(0, kpidSize, &size), S_OK);
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(18));
        CMyComPtr<IGetFolderArcProps> provider;
        QCOMPARE(opening.Folder.QueryInterface(IID_IGetFolderArcProps, &provider), S_OK);
        CMyComPtr<IFolderArcProps> props;
        QCOMPARE(provider->GetFolderArcProps(&props), S_OK);
        UInt32 levels = 0;
        QCOMPARE(props->GetArcNumLevels(&levels), S_OK);
        QVERIFY(levels > 0);
        NWindows::NCOM::CPropVariant type, rejected;
        QCOMPARE(props->GetArcProp(levels - 1, kpidType, &type), S_OK);
        QCOMPARE(type.vt, VARTYPE(VT_BSTR));
        QCOMPARE(ToQString(type.bstrVal), nativeType);
        QCOMPARE(props->GetArcProp(levels - 1, kpidErrorType, &rejected), S_OK);
        QCOMPARE(rejected.vt, VARTYPE(VT_BSTR));
        QCOMPARE(ToQString(rejected.bstrVal), rejectedType);
        // Original CFfpOpen publishes only NonOpen errors in ErrorMessage;
        // diagnostics on an opened level belong to the properties collector.
        QVERIFY(opening.ErrorMessage.IsEmpty());
        CPanel panel(opening.Folder);
        CRecordVector<UInt32> selected;
        PropertyProjection dialog(panel, selected);
        bool foundRejectedType = false;
        for (unsigned i = 0; i < dialog.Strings.Size(); ++i)
            if (dialog.Strings[i] == GetNameOfProperty(kpidErrorType, nullptr)) {
                QCOMPARE(ToQString(dialog.Values[i]), rejectedType);
                foundRejectedType = true;
            }
        QVERIFY(foundRejectedType);
        for (const QSize size : {QSize(540, 360), QSize(360, 240)}) {
            dialog.resize(size);
            dialog.show();
            QApplication::processEvents();
            auto *list = dialog.findChild<QTreeWidget *>("propertiesList");
            QVERIFY(list);
            QVERIFY(UiInspection::fullyVisible(*list, dialog));
            const auto rows = list->findItems(ToQString(GetNameOfProperty(kpidErrorType, nullptr)),
                                              Qt::MatchExactly, 0);
            QCOMPARE(rows.size(), 1);
            QCOMPARE(rows.first()->text(1), rejectedType);
            QVERIFY(UiInspection::writeReport(
                dialog, QStringLiteral("native-error-type-%1-%2").arg(format).arg(size.width())));
        }
        QCOMPARE(backend.loadCodecCatalog(fixture.filePath("missing module"), catalog->version()),
                 E_FAIL);
        native.Release();
        catalog.reset();
        QVERIFY(!retained.expired());
        QCOMPARE(props->GetArcProp(levels - 1, kpidErrorType, &rejected), S_OK);
        QCOMPARE(rejected.vt, VARTYPE(VT_BSTR));
        QCOMPARE(ToQString(rejected.bstrVal), rejectedType);
        opening.Folder.Release();
        props.Release();
        provider.Release();
        panel.ReleaseFolder();
        QVERIFY(retained.expired());
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), original);
    }
    void nativeLevelsAndNonOpenDiagnostics_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("nested TAR split") << "nested";
        QTest::newRow("rejected TAR in split") << "nonopen";
        QTest::newRow("concatenated ZIP parser") << "parser";
    }
    void nativeLevelsAndNonOpenDiagnostics() {
        QFETCH(QString, kind);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QFile input(fixture.filePath(QStringLiteral("исходный 空 файл.bin")));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("native levels"), qint64(13));
        input.close();
        QString path;
        const QString type = kind == "parser" ? "#" : QString();
        if (kind == "parser") {
            path = fixture.filePath("parser.unknown");
            QVERIFY(create("zip", path, input.fileName()));
            QFile archive(path);
            QVERIFY(archive.open(QIODevice::ReadOnly));
            const QByteArray bytes = archive.readAll();
            archive.close();
            QVERIFY(archive.open(QIODevice::Append));
            QCOMPARE(archive.write(bytes), qint64(bytes.size()));
        } else {
            const QString tar = fixture.filePath(QStringLiteral("внутренний 空 архив.tar"));
            if (kind == "nested")
                QVERIFY(create("tar", tar, input.fileName()));
            else {
                QFile broken(tar);
                QVERIFY(broken.open(QIODevice::WriteOnly));
                QCOMPARE(broken.write("not a TAR archive"), qint64(17));
            }
            // Original Split supplies a seekable inner stream for OpenArchive.
            // Gzip supplies no IInArchiveGetStream and cannot exercise levels.
            QFile bytes(tar);
            QVERIFY(bytes.open(QIODevice::ReadOnly));
            const QByteArray payload = bytes.readAll();
            const QString base = fixture.filePath(QStringLiteral("внешний 空 архив.tar."));
            path = base + "001";
            for (int part = 0; part < 2; ++part) {
                QFile volume(base + (part ? "002" : "001"));
                QVERIFY(volume.open(QIODevice::WriteOnly));
                const QByteArray data =
                    part ? payload.mid(payload.size() / 2) : payload.left(payload.size() / 2);
                QCOMPARE(volume.write(data), qint64(data.size()));
            }
        }
        ArchiveBackend backend(nullptr, engine_);
        QSignalSpy ready(&backend, &ArchiveBackend::codecCatalogReady);
        QVERIFY(backend.discoverCodecCatalog(module_));
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY2(ready[0][0].toBool(), qPrintable(ready[0][1].toString()));
        const auto catalog = backend.codecCatalog();
        COpenUi callback;
        CArchiveLink native;
        QCOMPARE(catalog->openArchive(native, path, type, &callback), S_OK);
        QCOMPARE(native.Arcs.Size(), kind == "nested" ? 2u : 1u);
        if (kind == "nonopen")
            QVERIFY(native.NonOpen_ErrorInfo.ErrorFormatIndex >= 0);
        if (kind == "parser")
            QVERIFY(native.GetArc()->FormatIndex < 0);
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(ToUString(path)), ToUString(type),
                                              UString(), nullptr),
                 S_OK);
        CMyComPtr<IGetFolderArcProps> provider;
        QCOMPARE(opening.Folder.QueryInterface(IID_IGetFolderArcProps, &provider), S_OK);
        CMyComPtr<IFolderArcProps> props;
        QCOMPARE(provider->GetFolderArcProps(&props), S_OK);
        UInt32 levels = 0;
        QCOMPARE(props->GetArcNumLevels(&levels), S_OK);
        QCOMPARE(levels, native.Arcs.Size());
        for (UInt32 level = 0; level < levels; ++level) {
            const auto &arc = native.Arcs[level];
            NWindows::NCOM::CPropVariant value;
            QCOMPARE(props->GetArcProp(level, kpidType, &value), S_OK);
            QCOMPARE(value.vt, VARTYPE(VT_BSTR));
            QCOMPARE(ToQString(value.bstrVal),
                     arc.FormatIndex < 0
                         ? QString("Parser")
                         : ToQString(catalog->nativeCodecs().Formats[arc.FormatIndex].Name));
            QCOMPARE(props->GetArcProp(level, kpidPath, &value), S_OK);
            QCOMPARE(ToQString(value.bstrVal), ToQString(arc.Path));
            UInt32 count = 0, expected = 0;
            QCOMPARE(props->GetArcNumProps(level, &count), S_OK);
            QCOMPARE(arc.Archive->GetNumberOfArchiveProperties(&expected), S_OK);
            QCOMPARE(count, expected);
            for (UInt32 i = 0; i < count; ++i) {
                CMyComBSTR name, nativeName;
                PROPID id = 0, nativeId = 0;
                VARTYPE vt = VT_EMPTY, nativeVt = VT_EMPTY;
                QCOMPARE(props->GetArcPropInfo(level, i, &name, &id, &vt), S_OK);
                QCOMPARE(arc.Archive->GetArchivePropertyInfo(i, &nativeName, &nativeId, &nativeVt),
                         S_OK);
                QCOMPARE(id, nativeId);
                QCOMPARE(vt, nativeVt);
                QCOMPARE(QString::fromWCharArray(name), QString::fromWCharArray(nativeName));
            }
            if (level) {
                QCOMPARE(props->GetArcNumProps2(level, &count), S_OK);
                QCOMPARE(native.Arcs[level - 1].Archive->GetNumberOfProperties(&expected), S_OK);
                QCOMPARE(count, expected);
                for (UInt32 i = 0; i < count; ++i) {
                    CMyComBSTR name, nativeName;
                    PROPID id = 0, nativeId = 0;
                    VARTYPE vt = VT_EMPTY, nativeVt = VT_EMPTY;
                    QCOMPARE(props->GetArcPropInfo2(level, i, &name, &id, &vt), S_OK);
                    QCOMPARE(native.Arcs[level - 1].Archive->GetPropertyInfo(i, &nativeName,
                                                                             &nativeId, &nativeVt),
                             S_OK);
                    QCOMPARE(id, nativeId);
                    QCOMPARE(vt, nativeVt);
                    QCOMPARE(QString::fromWCharArray(name), QString::fromWCharArray(nativeName));
                }
                QCOMPARE(props->GetArcProp2(level, kpidSize, &value), S_OK);
                QCOMPARE(value.vt, VARTYPE(VT_UI8));
                QCOMPARE(
                    value.uhVal.QuadPart,
                    UInt64(QFileInfo(fixture.filePath(QStringLiteral("внутренний 空 архив.tar")))
                               .size()));
            }
        }
        NWindows::NCOM::CPropVariant rejected;
        QCOMPARE(props->GetArcProp(levels, kpidErrorType, &rejected), S_OK);
        if (kind == "nonopen") {
            const QString expected = ToQString(
                catalog->nativeCodecs().Formats[native.NonOpen_ErrorInfo.ErrorFormatIndex].Name);
            QCOMPARE(rejected.vt, VARTYPE(VT_BSTR));
            QCOMPARE(ToQString(rejected.bstrVal), expected);
            QVERIFY(ToQString(opening.ErrorMessage).contains(expected));
        } else
            QCOMPARE(rejected.vt, VARTYPE(VT_EMPTY));
    }
};
QTEST_MAIN(NativeArchivePropertiesTests)
#include "native_archive_properties_tests.moc"
