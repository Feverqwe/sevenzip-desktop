#include "archive/support/source_extract_fixture.h"
#include "formats/rar/support/rar_memory_fixture.h"
#include "operations/CompressCall.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <UIAutomation.h>
#include <atomic>
#include <cstdio>
#include <thread>

namespace {
struct AddDialog {
    DWORD process = 0;
    HWND window = nullptr;
    QString title = QStringLiteral("Add to Archive");
};
BOOL CALLBACK FindAddDialog(HWND window, LPARAM parameter) {
    auto &dialog = *reinterpret_cast<AddDialog *>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != dialog.process || !IsWindowVisible(window))
        return TRUE;
    wchar_t title[256]{};
    GetWindowTextW(window, title, 256);
    if (!QString::fromWCharArray(title).contains(dialog.title, Qt::CaseInsensitive))
        return TRUE;
    dialog.window = window;
    return FALSE;
}
// The fixture host owns only its disposable child process tree. Closing the
// host's job on a test timeout also releases a helper stuck in a modal dialog.
class ChildJob {
    HANDLE handle = CreateJobObjectW(nullptr, nullptr);

  public:
    DWORD helperProcess() const {
        struct Processes {
            DWORD assigned;
            DWORD count;
            ULONG_PTR ids[16];
        } processes{};
        if (!QueryInformationJobObject(handle, JobObjectBasicProcessIdList, &processes,
                                       sizeof(processes), nullptr))
            return 0;
        for (DWORD i = 0; i < processes.count; ++i)
            if (processes.ids[i] != GetCurrentProcessId())
                return DWORD(processes.ids[i]);
        return 0;
    }
    bool attach() {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        return handle &&
               SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits,
                                       sizeof(limits)) &&
               AssignProcessToJobObject(handle, GetCurrentProcess());
    }
    ~ChildJob() {
        if (handle) {
            // Normal return follows the source waitFinish join. A forced host
            // termination bypasses this disarm and kills its helper as well.
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits,
                                    sizeof(limits));
            CloseHandle(handle);
        }
    }
};

QStringList AccessibleNames(HWND window, QHash<QString, bool> *exposed = nullptr) {
    if (exposed)
        exposed->clear();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto uninitialize = qScopeGuard([&] {
        if (SUCCEEDED(initialized))
            CoUninitialize();
    });
    CMyComPtr<IUIAutomation> automation;
    if (FAILED(automation.CoCreateInstance(__uuidof(CUIAutomation), __uuidof(IUIAutomation))))
        return {};
    CMyComPtr<IUIAutomationElement> root;
    CMyComPtr<IUIAutomationCondition> condition;
    CMyComPtr<IUIAutomationElementArray> elements;
    if (FAILED(automation->ElementFromHandle(window, &root)) ||
        FAILED(automation->CreateTrueCondition(&condition)) ||
        FAILED(root->FindAll(TreeScope_Descendants, condition, &elements)))
        return {};
    int length = 0;
    if (FAILED(elements->get_Length(&length)))
        return {};
    QStringList names;
    for (int i = 0; i < length; ++i) {
        CMyComPtr<IUIAutomationElement> element;
        if (FAILED(elements->GetElement(i, &element)))
            continue;
        BSTR name = nullptr;
        if (SUCCEEDED(element->get_CurrentName(&name)) && name) {
            const QString text = QString::fromWCharArray(name, int(SysStringLen(name)));
            names.append(text);
            if (exposed) {
                BOOL enabled = FALSE, offscreen = TRUE;
                const bool available = SUCCEEDED(element->get_CurrentIsEnabled(&enabled)) &&
                                       SUCCEEDED(element->get_CurrentIsOffscreen(&offscreen));
                exposed->insert(text, available && enabled && !offscreen);
            }
        }
        SysFreeString(name);
    }
    return names;
}

enum class AccessibleAction { Expand, Collapse, Check };

bool ActivateAccessibleControl(HWND window, const wchar_t *name, AccessibleAction action) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto uninitialize = qScopeGuard([&] {
        if (SUCCEEDED(initialized))
            CoUninitialize();
    });
    CMyComPtr<IUIAutomation> automation;
    if (FAILED(automation.CoCreateInstance(__uuidof(CUIAutomation), __uuidof(IUIAutomation))))
        return false;
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    const auto clear = qScopeGuard([&] { VariantClear(&value); });
    if (!value.bstrVal)
        return false;
    CMyComPtr<IUIAutomationElement> root, element;
    CMyComPtr<IUIAutomationCondition> condition;
    CMyComPtr<IUnknown> pattern;
    if (FAILED(automation->ElementFromHandle(window, &root)) ||
        FAILED(automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition)) ||
        FAILED(root->FindFirst(TreeScope_Descendants, condition, &element)) || !element)
        return false;
    if (action != AccessibleAction::Check) {
        CMyComPtr<IUIAutomationExpandCollapsePattern> expansion;
        if (FAILED(element->GetCurrentPattern(UIA_ExpandCollapsePatternId, &pattern)) || !pattern ||
            FAILED(
                pattern.QueryInterface(__uuidof(IUIAutomationExpandCollapsePattern), &expansion)))
            return false;
        const bool expand = action == AccessibleAction::Expand;
        const HRESULT invoked = expand ? expansion->Expand() : expansion->Collapse();
        ExpandCollapseState state;
        return SUCCEEDED(invoked) && SUCCEEDED(expansion->get_CurrentExpandCollapseState(&state)) &&
               state == (expand ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
    }
    CMyComPtr<IUIAutomationTogglePattern> toggle;
    if (FAILED(element->GetCurrentPattern(UIA_TogglePatternId, &pattern)) || !pattern ||
        FAILED(pattern.QueryInterface(__uuidof(IUIAutomationTogglePattern), &toggle)))
        return false;
    ToggleState state;
    return SUCCEEDED(toggle->get_CurrentToggleState(&state)) &&
           (state == ToggleState_On || SUCCEEDED(toggle->Toggle()));
}
} // namespace

class NativeGUIWorkflowTests final : public QObject {
    Q_OBJECT
    QString engine;
    void verifyArchive(const QString &archive) {
        QProcess process;
        process.start(engine, {"t", archive});
        QVERIFY(process.waitForFinished(10000));
        QVERIFY2(process.exitCode() == 0, process.readAll().constData());
    }

  private slots:
    void initTestCase() {
        engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!engine.isEmpty());
        QVERIFY(QFileInfo::exists(QString::fromUtf8(NATIVE_GUI_EXECUTABLE)));
    }
    void nativeSenderCreatesUnicodeArchive() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(SourceExtractFixture::kName);
        const QByteArray bytes("native sender/map/GUI payload");
        QVERIFY(SourceExtractFixture::writeFile(source, bytes));
        const QString archive = fixture.filePath(QStringLiteral("результат 空 with spaces.zip"));
        QProcess host;
        host.start(
            QCoreApplication::applicationFilePath(),
            {"--native-add", QDir::toNativeSeparators(archive), QDir::toNativeSeparators(source)});
        QVERIFY(host.waitForStarted(5000));
        QVERIFY2(host.waitForFinished(30000), host.readAllStandardError().constData());
        QCOMPARE(host.exitStatus(), QProcess::NormalExit);
        QCOMPARE(host.exitCode(), 0);
        QVERIFY(QFileInfo::exists(archive));
        verifyArchive(archive);
        QProcess extract;
        const QString output = fixture.filePath(QStringLiteral("проверка 空"));
        extract.start(engine, {"x", "-y", "-o" + output, archive});
        QVERIFY(extract.waitForFinished(10000));
        QCOMPARE(extract.exitCode(), 0);
        QCOMPARE(
            SourceExtractFixture::readFile(QDir(output).filePath(QFileInfo(source).fileName())),
            bytes);
        QCOMPARE(SourceExtractFixture::readFile(source), bytes);
    }
    void guiArchiveCommands_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("excluded");
        QTest::addColumn<bool>("broken");
        QTest::newRow("test") << QString("t") << false << false;
        QTest::newRow("extract") << QString("x") << false << false;
        QTest::newRow("excluded-handler") << QString("x") << true << false;
        QTest::newRow("broken-archive") << QString("t") << false << true;
    }
    void guiArchiveCommands() {
        QFETCH(QString, command);
        QFETCH(bool, excluded);
        QFETCH(bool, broken);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("native helper payload");
        const QString original = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!original.isEmpty());
        const QString archive = fixture.filePath(QStringLiteral("архив 密碼 with spaces.zip"));
        QVERIFY(QFile::rename(original, archive));
        if (broken)
            QVERIFY(SourceExtractFixture::writeFile(archive, QByteArray("broken ZIP")));
        const QByteArray before = SourceExtractFixture::readFile(archive);
        const QString output = fixture.filePath(QStringLiteral("выход 空"));
        const QString sentinel = QDir(output).filePath("existing.bin");
        QVERIFY(SourceExtractFixture::writeFile(sentinel, QByteArray("keep")));
        QStringList arguments{command, "-y", "-o" + QDir::toNativeSeparators(output)};
        if (excluded)
            arguments << "-stxzip";
        arguments << "--" << QDir::toNativeSeparators(archive);
        QProcess helper;
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE), arguments);
        QVERIFY(helper.waitForStarted(5000));
        QVERIFY2(helper.waitForFinished(30000), "native GUI command did not complete");
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), excluded || broken ? 2 : 0);
        const QString extracted = QDir(output).filePath(SourceExtractFixture::kName);
        if (command == "x" && !excluded)
            QCOMPARE(SourceExtractFixture::readFile(extracted), bytes);
        else
            QVERIFY(!QFileInfo::exists(extracted));
        QCOMPARE(SourceExtractFixture::readFile(sentinel), QByteArray("keep"));
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
    }
    void userCancelsSourceAddDialog() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(SourceExtractFixture::kName);
        const QByteArray bytes("cancel keeps input");
        QVERIFY(SourceExtractFixture::writeFile(source, bytes));
        const QString archive = fixture.filePath(QStringLiteral("отмена 空 with spaces.zip"));
        QProcess helper;
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE),
                     {"a", "-ad", "-tzip", "--", QDir::toNativeSeparators(archive),
                      QDir::toNativeSeparators(source)});
        QVERIFY(helper.waitForStarted(5000));
        AddDialog dialog{DWORD(helper.processId())};
        QTRY_VERIFY_WITH_TIMEOUT(
            (EnumWindows(FindAddDialog, reinterpret_cast<LPARAM>(&dialog)), dialog.window), 20000);
        for (const auto action :
             {AccessibleAction::Expand, AccessibleAction::Collapse, AccessibleAction::Expand})
            QTRY_VERIFY_WITH_TIMEOUT(
                ActivateAccessibleControl(dialog.window, L"Compression settings", action), 5000);
        QVERIFY(PostMessageW(dialog.window, WM_CLOSE, 0, 0));
        QVERIFY(helper.waitForFinished(10000));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 255); // original kUserBreak
        QVERIFY(!QFileInfo::exists(archive));
        QCOMPARE(SourceExtractFixture::readFile(source), bytes);
    }
    void userCancelsSourceExtractDialog() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray bytes("source Extract cancellation keeps input");
        const QString archive = SourceExtractFixture::makeArchive(fixture.path(), bytes);
        QVERIFY(!archive.isEmpty());
        const QByteArray before = SourceExtractFixture::readFile(archive);
        const QString output = fixture.filePath(QStringLiteral("назначение 空"));
        QProcess helper;
        const auto stop = qScopeGuard([&] {
            if (helper.state() != QProcess::NotRunning) {
                helper.kill();
                helper.waitForFinished(5000);
            }
        });
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE),
                     {"x", "-ad", "-o" + QDir::toNativeSeparators(output), "--",
                      QDir::toNativeSeparators(archive)});
        QVERIFY(helper.waitForStarted(5000));
        AddDialog dialog{DWORD(helper.processId()), nullptr, "Extract"};
        QTRY_VERIFY_WITH_TIMEOUT(
            (EnumWindows(FindAddDialog, reinterpret_cast<LPARAM>(&dialog)), dialog.window), 20000);
        wchar_t caption[1024]{};
        QVERIFY(GetWindowTextW(dialog.window, caption, 1024) > 0);
        QCOMPARE(QString::fromWCharArray(caption),
                 "Extract : " + QDir::toNativeSeparators(archive));
        QTRY_VERIFY_WITH_TIMEOUT(ActivateAccessibleControl(dialog.window, L"Advanced settings",
                                                           AccessibleAction::Expand),
                                 5000);
        QHash<QString, bool> exposed;
        const QString advancedControl = "Eliminate duplication of root folder";
        QTRY_VERIFY_WITH_TIMEOUT(
            (AccessibleNames(dialog.window, &exposed), exposed.value(advancedControl)), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ActivateAccessibleControl(dialog.window, L"Advanced settings",
                                                           AccessibleAction::Collapse),
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (AccessibleNames(dialog.window, &exposed), !exposed.value(advancedControl)), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(ActivateAccessibleControl(dialog.window, L"Advanced settings",
                                                           AccessibleAction::Expand),
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            ActivateAccessibleControl(dialog.window, L"Use a password", AccessibleAction::Check),
            5000);
        for (const QString &name : {QString("Path mode:"), QString("Overwrite mode:"),
                                    QString("Eliminate duplication of root folder"),
                                    QString("Password"), QString("Show Password")}) {
            QTRY_VERIFY2_WITH_TIMEOUT(
                (AccessibleNames(dialog.window, &exposed), exposed.value(name)),
                qPrintable(name + ": " + exposed.keys().join(" | ")), 5000);
        }
        QVERIFY(PostMessageW(dialog.window, WM_CLOSE, 0, 0));
        QVERIFY(helper.waitForFinished(10000));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 255);
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
        QCOMPARE(SourceExtractFixture::readFile(fixture.filePath(SourceExtractFixture::kName)),
                 bytes);
        QVERIFY(!QFileInfo::exists(output));
    }
    void yesToAllRetainsSourceMemoryRefusal() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString archive = fixture.filePath(QStringLiteral("память 空 with spaces.rar"));
        const QByteArray before = RarMemoryFixture::archive(0);
        QVERIFY(SourceExtractFixture::writeFile(archive, before));
        const QString output = fixture.filePath(QStringLiteral("выход 空"));
        QProcess helper;
        // The stored fixture declares a 2 MiB dictionary. The official handler
        // asks the source callback about its forced 1 MiB limit, even for Copy.
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE),
                     {"x", "-y", "-mmemx=1m", "-o" + QDir::toNativeSeparators(output), "--",
                      QDir::toNativeSeparators(archive)});
        QVERIFY(helper.waitForStarted(5000));
        QVERIFY2(helper.waitForFinished(30000), "-y must not wait for a memory question");
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 2);
        QVERIFY(!QFileInfo::exists(QDir(output).filePath(RarMemoryFixture::name())));
        QCOMPARE(SourceExtractFixture::readFile(archive), before);
    }
    void nativeHashShowsActualDigest() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("hash данные 空 with spaces.bin"));
        const QByteArray bytes("actual native GUI SHA256 payload");
        QVERIFY(SourceExtractFixture::writeFile(source, bytes));
        QProcess helper;
        const auto stop = qScopeGuard([&] {
            if (helper.state() != QProcess::NotRunning) {
                helper.kill();
                helper.waitForFinished(5000);
            }
        });
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE),
                     {"h", "-y", "-scrcSHA256", "--", QDir::toNativeSeparators(source)});
        QVERIFY(helper.waitForStarted(5000));
        AddDialog dialog{DWORD(helper.processId()), nullptr, "Checksum information"};
        QTRY_VERIFY_WITH_TIMEOUT(
            (EnumWindows(FindAddDialog, reinterpret_cast<LPARAM>(&dialog)), dialog.window), 20000);
        const QString expected = QString::fromLatin1(
            QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const QString names = AccessibleNames(dialog.window).join("\n");
        QVERIFY2(names.contains("SHA256"), qPrintable(names));
        QVERIFY2(names.contains(expected, Qt::CaseInsensitive), qPrintable(names));
        QVERIFY2(names.contains(QFileInfo(source).fileName()), qPrintable(names));
        QVERIFY(PostMessageW(dialog.window, WM_CLOSE, 0, 0));
        QVERIFY(helper.waitForFinished(10000));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 0);
        QCOMPARE(SourceExtractFixture::readFile(source), bytes);
    }
    void nativeBenchmarkJoinsAfterClose_data() {
        QTest::addColumn<bool>("total");
        QTest::newRow("native-benchmark") << false;
        QTest::newRow("native-total-benchmark") << true;
    }
    void nativeBenchmarkJoinsAfterClose() {
        QFETCH(bool, total);
        QProcess helper;
        const auto stop = qScopeGuard([&] {
            if (helper.state() != QProcess::NotRunning) {
                helper.kill();
                helper.waitForFinished(5000);
            }
        });
        QStringList arguments{"b", "-mmt1", "-md18"};
        if (total)
            arguments << "-mm=*";
        helper.start(QString::fromUtf8(NATIVE_GUI_EXECUTABLE), arguments);
        QVERIFY(helper.waitForStarted(5000));
        AddDialog dialog{DWORD(helper.processId()), nullptr, "Benchmark"};
        QTRY_VERIFY_WITH_TIMEOUT(
            (EnumWindows(FindAddDialog, reinterpret_cast<LPARAM>(&dialog)), dialog.window), 20000);
        QVERIFY(PostMessageW(dialog.window, WM_CLOSE, 0, 0));
        QVERIFY(helper.waitForFinished(10000));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 255); // Closing an active benchmark reports cancellation.
    }
};

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const QStringList args = application.arguments();
    if (args.size() == 4 && args[1] == "--native-add") {
        ChildJob job;
        if (!job.attach())
            return 101;
        UStringVector names;
        names.Add(args[3].toStdWString().c_str());
        std::atomic<bool> completed{false};
        std::thread observer([&] {
            for (int i = 0; i < 250 && !completed.load(); ++i)
                Sleep(100);
            if (!completed.load()) {
                AddDialog dialog{job.helperProcess(), nullptr, QString()};
                EnumWindows(FindAddDialog, reinterpret_cast<LPARAM>(&dialog));
                const QByteArray diagnostics = AccessibleNames(dialog.window).join("\n").toUtf8();
                std::fprintf(stderr, "Source Add timeout, owned helper %lu:\n%s\n",
                             static_cast<unsigned long>(dialog.process), diagnostics.constData());
                std::fflush(stderr);
            }
        });
        const HRESULT result = CompressFiles(UString(), UString(args[2].toStdWString().c_str()),
                                             UString("zip"), false, names, false, false, true);
        completed = true;
        observer.join();
        return result == S_OK ? 0 : 102;
    }
    NativeGUIWorkflowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "native_gui_workflow_tests.moc"
