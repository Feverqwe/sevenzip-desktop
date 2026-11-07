#include "archive/operations/ZipRegistry.h"
#include "benchmark/Bench.h"
#include "operations/CompressCall.h"
#include "platform/memory/qtmemorylockservices.h"
#include "settings/RegistryUtils.h"
#include "settings/SettingsPage.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

extern bool g_LargePagesMode;

class MemoryLockRecorder final : public IQtMemoryLockServices {
  public:
    bool supported = true;
    bool enabledResult = true;
    unsigned risk = 0;
    mutable QStringList calls;
    bool IsSupported() const override { return supported; }
    bool AddLockMemoryPrivilege() const override { return false; }
    unsigned Get_LargePages_RiskLevel() const override { return risk; }
    bool EnablePrivilege_LockMemory(bool enable) const override {
        calls << (enable ? "enable" : "disable");
        return enabledResult;
    }
};

class MemoryLockTests : public QObject {
    Q_OBJECT
  private slots:
    void startupPolicy_data() {
        QTest::addColumn<bool>("supported");
        QTest::addColumn<bool>("saved");
        QTest::addColumn<unsigned>("risk");
        QTest::addColumn<bool>("enabledResult");
        QTest::newRow("unsupported") << false << true << 0u << true;
        QTest::newRow("disabled") << true << false << 0u << true;
        QTest::newRow("risk-1") << true << true << 1u << true;
        QTest::newRow("risk-2") << true << true << 2u << true;
        QTest::newRow("privilege-refused") << true << true << 0u << false;
        QTest::newRow("enabled-despite-add-failure") << true << true << 0u << true;
    }
    void startupPolicy() {
        QFETCH(bool, supported);
        QFETCH(bool, saved);
        QFETCH(unsigned, risk);
        QFETCH(bool, enabledResult);
        QTemporaryDir fixture;
        const QString path = fixture.filePath(QStringLiteral("настройки 空 memory.ini"));
        SaveLockMemoryEnable(saved, path);
        MemoryLockRecorder services;
        services.supported = supported;
        services.risk = risk;
        services.enabledResult = enabledResult;
        const bool originalMode = g_LargePagesMode;
        const auto restore = qScopeGuard([&] { g_LargePagesMode = originalMode; });
        for (bool previous : {false, true}) {
            g_LargePagesMode = previous;
            services.calls.clear();
            NQtFileManager::SetMemoryLock(path, services);
            QCOMPARE(services.calls.contains("enable"), supported && saved && risk == 0);
            QVERIFY(!services.calls.contains("disable"));
            QCOMPARE(g_LargePagesMode, supported && saved && risk == 0 ? enabledResult : previous);
            QCOMPARE(ReadLockMemoryEnable(path), saved);
#ifdef Q_OS_WIN
            AString diagnostic;
            Add_LargePages_String(diagnostic);
            QCOMPARE(diagnostic.Find("(LP-") >= 0, g_LargePagesMode);
#endif
        }
    }
    void engineSwitchUsesSourceRiskPolicy() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("memory.ini");
        MemoryLockRecorder services;
        for (bool enable : {false, true}) {
            SaveLockMemoryEnable(enable, path);
            for (unsigned risk : {0u, 1u, 2u}) {
                services.risk = risk;
                services.calls.clear();
                UString params("existing");
                AddLagePagesSwitch(params, path, &services);
                QCOMPARE(QString::fromWCharArray(params),
                         enable && risk == 0 ? QString("existing -slp") : QString("existing"));
            }
        }
    }
    void changingExtractionLimitPreservesEnginePolicy() {
        QTemporaryDir fixture;
        const QString path = fixture.filePath("memory.ini");
        for (bool enabled : {false, true}) {
            SaveLockMemoryEnable(enabled, path);
            CSettingsPage page(path);
            page.findChild<QCheckBox *>("extractMemorySet")->setChecked(true);
            page.findChild<QLineEdit *>("extractMemoryEdit")->setText("7");
            QCOMPARE(page.OnApply(), 0L);
            QCOMPARE(NExtract::Read_LimitGB(path), UInt32(7));
            QCOMPARE(ReadLockMemoryEnable(path), enabled);
        }
    }
};
QTEST_MAIN(MemoryLockTests)
#include "memory_lock_tests.moc"
