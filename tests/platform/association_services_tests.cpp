#include "platform/associations/qtassociationplatform.h"
#include <QtTest>
#include <functional>
#include <utility>
#ifdef _WIN32
#include <QScopeGuard>
#include <QSettings>
#include <QUuid>
#endif

namespace {
class CFakePlatform final : public IQtAssociationPlatform {
  public:
    QByteArray revision = "original";
    QByteArray scope;
    LONG readError = 0;
    LONG writeError = 0;
    int writes = 0;
    bool associated = false;
    std::function<void()> beforeRead;
    bool CanApply(unsigned group) const override { return group == 0; }
    QString UnavailableReason(unsigned) const override { return "Only current user is supported"; }
    UString OwnIconPath() const override { return UString(L"/app/папка с пробелом/sevenzip"); }
    LONG Read(unsigned, const UString &, QtAssociationRecord &record) override {
        if (auto callback = std::exchange(beforeRead, {}))
            callback();
        if (readError)
            return readError;
        record.revision = revision;
        record.scope = scope;
        record.info.ProgramKey = associated ? "7-Zip.fixture" : "Other.application";
        record.info.IconPath = associated ? OwnIconPath() : UString(L"/other");
        return 0;
    }
    LONG Write(unsigned, const UString &, bool associate, const QtAssociationRecord &,
               const UString &, const UString &, const UString &, int) override {
        writes++;
        if (writeError)
            return writeError;
        associated = associate;
        revision += "-write";
        return 0;
    }
    void NotifyChanged() override {}
};
} // namespace

class AssociationServicesTests final : public QObject {
    Q_OBJECT
  private slots:
    void absentAssociationRevisionIgnoresUnrelatedClasses() {
#ifdef _WIN32
        const QString token = QUuid::createUuid().toString(QUuid::Id128);
        const UString extension(qPrintable("sevenzipdesktop_missing_" + token));
        QSettings unrelated("HKEY_CURRENT_USER\\Software\\Classes\\SevenzipDesktopRevision." +
                                token,
                            QSettings::NativeFormat);
        const auto cleanup = qScopeGuard([&] {
            unrelated.remove("");
            unrelated.sync();
        });
        unrelated.setValue("PrivateFixture", 1);
        unrelated.sync();
        QCOMPARE(unrelated.status(), QSettings::NoError);
        const auto platform = CreateNativeAssociationPlatform();
        QtAssociationRecord before, after;
        QCOMPARE(platform->Read(0, extension, before), LONG(0));
        QVERIFY(before.info.ProgramKey.IsEmpty());
        unrelated.setValue("PrivateFixture", 2);
        unrelated.sync();
        QCOMPARE(platform->Read(0, extension, after), LONG(0));
        QVERIFY(after.info.ProgramKey.IsEmpty());
        QCOMPARE(after.revision, before.revision);
#else
        QSKIP("Native Windows registry revision contract.");
#endif
    }
    void nativeTypeAliasesFollowOwnWritesAndStillRejectExternalChanges() {
        auto native = std::make_shared<CFakePlatform>();
        native->scope = "fixture:0:application/gzip";
        auto service = CreateQtAssociationServices(native);
        NRegistryAssoc::CShellExtInfo gz, gzip;
        QVERIFY(service->Read(0, UString(L"gz"), gz));
        QVERIFY(service->Read(0, UString(L"gzip"), gzip));
        QCOMPARE(service->Add(0, UString(L"gz"), gz, UString(), UString(), UString(), 0), LONG(0));
        QCOMPARE(service->Add(0, UString(L"gzip"), gzip, UString(), UString(), UString(), 0),
                 LONG(0));
        QCOMPARE(native->writes, 2);
        native->revision = "external after own alias write";
        QCOMPARE(service->Delete(0, UString(L"gz"), gz), LONG(1237));
        QCOMPARE(native->writes, 2);
    }
    void detectApplyRevertAndRepeatedApply() {
        auto native = std::make_shared<CFakePlatform>();
        auto service = CreateQtAssociationServices(native);
        NRegistryAssoc::CShellExtInfo info;
        QVERIFY(service->Read(0, UString(L"архив"), info));
        QCOMPARE(service->Add(0, UString(L"архив"), info, UString(), UString(), UString(), 0),
                 LONG(0));
        QVERIFY(native->associated);
        QCOMPARE(service->Delete(0, UString(L"архив"), info), LONG(0));
        QVERIFY(!native->associated);
        QCOMPARE(native->writes, 2);
    }
    void externalChangeIsRefusedBeforeWrite() {
        auto native = std::make_shared<CFakePlatform>();
        auto service = CreateQtAssociationServices(native);
        NRegistryAssoc::CShellExtInfo info;
        QVERIFY(service->Read(0, UString(L"zip"), info));
        native->revision = "external change";
        QCOMPARE(service->Add(0, UString(L"zip"), info, UString(), UString(), UString(), 0),
                 LONG(1237));
        QCOMPARE(native->writes, 0);
        QVERIFY(!native->associated);
        QVERIFY(service->Read(0, UString(L"zip"), info));
        QCOMPARE(service->Add(0, UString(L"zip"), info, UString(), UString(), UString(), 0),
                 LONG(0));
        QVERIFY(native->associated);
    }
    void deniedAbsentAndFailedDetectionDoNotWrite() {
        auto native = std::make_shared<CFakePlatform>();
        auto service = CreateQtAssociationServices(native);
        NRegistryAssoc::CShellExtInfo info;
        QVERIFY(service->Read(0, UString(L"zip"), info));
        QCOMPARE(service->Add(1, UString(L"zip"), info, UString(), UString(), UString(), 0),
                 LONG(5));
        QCOMPARE(service->Delete(0, UString(L"missing"), info), LONG(50));
        native->readError = 5;
        QVERIFY(!service->Read(0, UString(L"zip"), info));
        native->readError = 0;
        QCOMPARE(service->Delete(0, UString(L"zip"), info), LONG(50));
        QCOMPARE(native->writes, 0);
    }
    void redetectionDuringApplyDoesNotAdoptExternalChange_data() {
        QTest::addColumn<bool>("associate");
        QTest::newRow("add") << true;
        QTest::newRow("delete") << false;
    }
    void redetectionDuringApplyDoesNotAdoptExternalChange() {
        QFETCH(bool, associate);
        auto native = std::make_shared<CFakePlatform>();
        native->associated = !associate;
        auto service = CreateQtAssociationServices(native);
        const UString extension(L"zip");
        NRegistryAssoc::CShellExtInfo info;
        QVERIFY(service->Read(0, extension, info));
        bool redetected = false;
        native->beforeRead = [&] {
            native->revision = "external change during apply";
            NRegistryAssoc::CShellExtInfo updated;
            redetected = service->Read(0, extension, updated);
        };
        const auto apply = [&] {
            return associate ? service->Add(0, extension, info, UString(), UString(), UString(), 0)
                             : service->Delete(0, extension, info);
        };
        const LONG conflict = apply();
        QVERIFY(redetected);
        QCOMPARE(conflict, LONG(1237));
        QCOMPARE(native->writes, 0);
        QCOMPARE(native->associated, !associate);
        QVERIFY(service->Read(0, extension, info));
        QCOMPARE(apply(), LONG(0));
        QCOMPARE(native->writes, 1);
        QCOMPARE(native->associated, associate);
    }
    void nativeFailurePreservesSnapshotAndHandler() {
        auto native = std::make_shared<CFakePlatform>();
        auto service = CreateQtAssociationServices(native);
        NRegistryAssoc::CShellExtInfo info;
        QVERIFY(service->Read(0, UString(L"zip"), info));
        native->writeError = 5;
        QCOMPARE(service->Add(0, UString(L"zip"), info, UString(), UString(), UString(), 0),
                 LONG(5));
        QVERIFY(!native->associated);
        QCOMPARE(native->revision, QByteArray("original"));
        native->writeError = 0;
        QCOMPARE(service->Add(0, UString(L"zip"), info, UString(), UString(), UString(), 0),
                 LONG(0));
    }
};
QTEST_MAIN(AssociationServicesTests)
#include "association_services_tests.moc"
