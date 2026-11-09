#include "Windows/PropVariant.h"
#include "archive/agent/Agent.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/PropertyName.h"
#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FileFolderPluginOpen.h"
#include "filemanager/providers/qtlocationfolder.h"
#include "filemanager/support/property_projection_support.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QtTest>

namespace {
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QString value(const CListViewDialog &dialog, const QString &key) {
    for (unsigned i = 0; i < dialog.Strings.Size(); ++i)
        if (ToQString(dialog.Strings[i]) == key)
            return ToQString(dialog.Values[i]);
    return {};
}
QString name(PROPID id) { return ToQString(GetNameOfProperty(id, nullptr)); }
QString fullValue() { return QStringLiteral("Unicode й 😀 value ").repeated(100) + "\nlast line"; }
} // namespace

class SourcePropertiesTests final : public QObject {
    Q_OBJECT
  private slots:
    void unsignedRangeAndUndefinedTransport() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("transport fixture.7z");
        QVERIFY(write(path, "transport fixture"));
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> owner = resource;
        resource->Info.format = "7z";
        resource->Info.properties.insert("Comment", fullValue());
        resource->Info.physicalSize = 17;
        ArchiveEntry huge;
        huge.path = "huge.txt";
        huge.sizeValue = UInt64(-1);
        huge.packedSizeValue = 0;
        huge.rawProperties.insert(kpidSha1, QByteArray(20, '\0'));
        huge.rawProperties.insert(kpidSha256, QByteArray(32, char(0xab)));
        ArchiveEntry undefined;
        undefined.path = "undefined.txt";
        undefined.size = -1;
        undefined.packedSize = -1;
        undefined.properties.insert("Checksum", "data:1024");
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({huge, undefined}, UString("7z"), &folder, owner), S_OK);
        NWindows::NCOM::CPropVariant size, packed;
        QCOMPARE(folder->GetProperty(0, kpidSize, &size), HRESULT(S_OK));
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(-1));
        QCOMPARE(folder->GetProperty(0, kpidPackSize, &packed), HRESULT(S_OK));
        QCOMPARE(packed.vt, VARTYPE(VT_UI8));
        QCOMPARE(packed.uhVal.QuadPart, UInt64(0));
        QCOMPARE(folder->GetProperty(1, kpidSize, &size), HRESULT(S_OK));
        QCOMPARE(size.vt, VARTYPE(VT_EMPTY));
        QCOMPARE(folder->GetProperty(1, kpidCRC, &size), HRESULT(S_OK));
        QCOMPARE(size.vt, VARTYPE(VT_EMPTY));
        CPanel panel(folder);
        CRecordVector<UInt32> indices;
        indices.Add(0);
        PropertyProjection dialog(panel, indices);
        QCOMPARE(value(dialog, name(kpidSize)), QString("18 446 744 073 709 551 615"));
        QCOMPARE(value(dialog, "Comment"), fullValue());
        indices.Clear();
        indices.Add(1);
        PropertyProjection unavailable(panel, indices);
        QCOMPARE(value(unavailable, "Checksum"), QString("data:1024"));
        CMyComPtr<IArchiveGetRawProps> raw;
        QCOMPARE(folder.QueryInterface(IID_IArchiveGetRawProps, &raw), HRESULT(S_OK));
        UInt32 count = 99;
        QCOMPARE(raw->GetNumRawProps(&count), HRESULT(S_OK));
        QCOMPARE(count, UInt32(2));
        const void *data = nullptr;
        UInt32 type = 0;
        QCOMPARE(raw->GetRawProp(0, kpidSha1, &data, &count, &type), HRESULT(S_OK));
        QCOMPARE(QByteArray(static_cast<const char *>(data), int(count)), QByteArray(20, '\0'));
        QCOMPARE(raw->GetRawProp(0, kpidSha256, &data, &count, &type), HRESULT(S_OK));
        QCOMPARE(QByteArray(static_cast<const char *>(data), int(count)),
                 QByteArray(32, char(0xab)));
        QCOMPARE(raw->GetRawProp(1, kpidSha256, &data, &count, &type), HRESULT(S_OK));
        QCOMPARE(count, UInt32(0));
        QVERIFY(!data);
    }
    void realEngineZipCrc() {
        QTemporaryDir fixture;
        const QString file = QStringLiteral("файл с пробелом.txt");
        QVERIFY(write(fixture.filePath(file), "hello"));
        const QString path = fixture.filePath("fixture.zip");
        QProcess creator;
        creator.setWorkingDirectory(fixture.path());
        creator.start(QStandardPaths::findExecutable("7z"), {"a", "-tzip", "--", path, file});
        QVERIFY(creator.waitForFinished(10000));
        QCOMPARE(creator.exitCode(), 0);
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CFfpOpen opening(manager);
        QCOMPARE(opening.OpenFileFolderPlugin(nullptr, us2fs(path.toStdWString().c_str()),
                                              UString(), UString(), nullptr),
                 HRESULT(S_OK));
        CPanel panel(opening.Folder);
        CRecordVector<UInt32> indices;
        indices.Add(0);
        PropertyProjection dialog(panel, indices);
        QCOMPARE(value(dialog, name(kpidName)).normalized(QString::NormalizationForm_C), file);
        QVERIFY(value(dialog, name(kpidPhySize)).toUInt() > 0);
        QVERIFY(!value(dialog, name(kpidType)).isEmpty());
        NWindows::NCOM::CPropVariant size;
        QCOMPARE(opening.Folder->GetProperty(0, kpidSize, &size), HRESULT(S_OK));
        QCOMPARE(size.vt, VARTYPE(VT_UI8));
        QCOMPARE(size.uhVal.QuadPart, UInt64(5));
        QCOMPARE(value(dialog, name(kpidCRC)), QString("3610A686"));
        QVERIFY(!value(dialog, "Method").isEmpty());
        QVERIFY(!backend.busy());
    }
    void fullArchiveValueAndLevels_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("normal") << QSize(540, 360);
        QTest::newRow("compact") << QSize(360, 240);
    }
    void fullArchiveValueAndLevels() {
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        const QString languageSettings = fixture.filePath("language.ini");
        const auto restoreLanguage = qScopeGuard([&] {
            SaveRegLang(UString("-"), languageSettings);
            ReloadLang(languageSettings);
        });
        SaveRegLang(UString("-"), languageSettings);
        ReloadLang(languageSettings);
        const QString path = fixture.filePath("owned metadata archive");
        QVERIFY(write(path, "transport fixture"));
        const QString full = fullValue();
        auto *resource = new QtArchiveFileResource(path);
        CMyComPtr<IUnknown> retain = resource;
        resource->Info.format = "7z";
        resource->Info.properties.insert("Comment", full);
        resource->Info.physicalSize = 17;
        CMyComPtr<IFolderFolder> folder;
        QCOMPARE(CreateArchiveFolder({ArchiveEntry{"inside/file", 0, 0}}, UString("7z"), &folder,
                                     retain),
                 HRESULT(S_OK));
        CMyComPtr<IGetFolderArcProps> owner;
        QCOMPARE(folder.QueryInterface(IID_IGetFolderArcProps, &owner), HRESULT(S_OK));
        CMyComPtr<IFolderArcProps> levels;
        QCOMPARE(owner->GetFolderArcProps(&levels), HRESULT(S_OK));
        auto *agent = dynamic_cast<CAgent *>((IFolderArcProps *)levels);
        QVERIFY(agent);
        CArc &inner = agent->_archiveLink.Arcs.AddNew();
        inner.Archive = agent->_archiveLink.Arcs[0].Archive;
        QCOMPARE(resource->ConfigureArc(inner), HRESULT(S_OK));
        inner.Path = L"inner.7z";
        inner.SubfileIndex = 0;
        agent->_archiveLink.NonOpen_ArcPath = L"rejected.child";
        agent->_archiveLink.NonOpen_ErrorInfo.ErrorFlags = kpv_ErrorFlags_CrcError;
        CPanel panel(folder);
        CRecordVector<UInt32> indices;
        PropertyProjection dialog(panel, indices);
        QCOMPARE(value(dialog, "Comment"), full);
        QVERIFY(!value(dialog, name(kpidErrorFlags)).isEmpty());
        QStringList paths;
        for (unsigned i = 0; i < dialog.Strings.Size(); ++i)
            if (ToQString(dialog.Strings[i]) == name(kpidPath))
                paths.append(QDir::fromNativeSeparators(ToQString(dialog.Values[i])));
        QCOMPARE(paths, QStringList({"inner.7z", "inside/file", path, "rejected.child"}));
        dialog.resize(size);
        dialog.show();
        QApplication::processEvents();
        auto *list = dialog.findChild<QTreeWidget *>("propertiesList");
        const auto rows = list->findItems("Comment", Qt::MatchExactly, 0);
        QVERIFY(!rows.isEmpty());
        list->setCurrentItem(rows.front());
        QVERIFY(rows.front()->text(1).endsWith(" ..."));
        const QString englishFlags = value(dialog, name(kpidErrorFlags));
        for (const char *language : {"ru", "-"}) {
            SaveRegLang(UString(language), languageSettings);
            ReloadLang(languageSettings);
            QtNotifyLanguageChanged();
            QCOMPARE(list->currentItem(), rows.front());
            if (QString(language) == "ru")
                QVERIFY(value(dialog, name(kpidErrorFlags)) != englishFlags);
            else
                QCOMPARE(value(dialog, name(kpidErrorFlags)), englishFlags);
            QCOMPARE(agent->_archiveLink.NonOpen_ErrorInfo.ErrorFlags,
                     UInt32(kpv_ErrorFlags_CrcError));
        }
        QTest::keyClick(list, Qt::Key_Insert, Qt::ControlModifier);
        QVERIFY(QApplication::clipboard()->text().startsWith("Comment: " + full));
        bool detail = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto *info = dialog.findChild<QDialog *>("listItemInfoDialog");
            QVERIFY(info);
            QCOMPARE(info->findChild<QPlainTextEdit *>()->toPlainText(), full);
            detail = true;
            info->reject();
        });
        QTest::keyClick(list, Qt::Key_Return);
        QVERIFY(detail && dialog.isVisible());
        QVERIFY(UiInspection::fullyVisible(*list, dialog));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(UiInspection::fullyVisible(*buttons, dialog));
        QVERIFY(UiInspection::separate(*list, *buttons, dialog));
        QVERIFY(
            UiInspection::writeReport(dialog, QString("source-properties-%1").arg(size.width())));
    }
};
QTEST_MAIN(SourcePropertiesTests)
#include "source_properties_tests.moc"
