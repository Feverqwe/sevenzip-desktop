#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "platform/associations/qtassociatedfileservices.h"
#include "platform/temp/qtownedtempregistry.h"
#include "settings/RegistryUtils.h"
#include "ui/support/layout_inspection.h"
#include <QAbstractButton>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

namespace {
class CAssociationService final : public IQtExternalFileServices {
  public:
    QString Opened;
    HRESULT Result = S_OK;
    HRESULT StartApplication(const UString &, const UString &path, QWidget *) override {
        Opened = ToQString(path);
        return Result; // Fake association with no handle; never starts a user application.
    }
};
bool write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class ExternalLaunchTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeDiskViewAndEditUseSystemAssociation() {
        QTemporaryDir fixture;
        const QString name = QStringLiteral("текст с пробелом.txt");
        QVERIFY(write(fixture.filePath(name), "contents"));
        const QString settings = fixture.filePath("settings.ini");
        QSettings legacy(settings, QSettings::IniFormat);
        legacy.setValue("FM/Viewer", "missing-viewer --legacy-argument");
        legacy.setValue("FM/Editor", "missing-editor --legacy-argument");
        legacy.sync();
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CAssociationService service;
        CPanel panel;
        panel.AttachFolderManager(manager, settings);
        panel.AttachExternalFileServices(&service);
        COpenResult result;
        QCOMPARE(panel.BindToPath(ToUString(fixture.path()), UString(), result), S_OK);
        panel.LoadFullPath();
        QCOMPARE(panel.GetFolder()->LoadItems(), S_OK);
        UInt32 count = 0;
        QCOMPARE(panel.GetFolder()->GetNumberOfItems(&count), S_OK);
        UInt32 index = count;
        for (UInt32 i = 0; i < count; ++i)
            if (QFileInfo(ToQString(panel.GetItemFullPath(i)))
                    .fileName()
                    .normalized(QString::NormalizationForm_C) == name)
                index = i;
        QVERIFY(index < count);
        const UString fullPath = panel.GetItemFullPath(index);
        QCOMPARE(QDir::fromNativeSeparators(ToQString(fullPath))
                     .normalized(QString::NormalizationForm_C),
                 fixture.filePath(name));
        for (bool editor : {false, true}) {
            service.Opened.clear();
            panel.EditItem(index, editor);
            QCOMPARE(service.Opened, ToQString(fullPath));
        }
    }
    void processPreservesArgumentsAndFailsWithoutExecutable() {
        QTemporaryDir fixture;
        const QString program = fixture.filePath(QStringLiteral("приложение с пробелом"))
#ifdef Q_OS_WIN
                                + ".exe"
#endif
            ;
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), program));
        const QString output = fixture.filePath("arguments.json");
        const QString first = fixture.filePath(QStringLiteral("первый файл.txt"));
        const QString second = fixture.filePath(QStringLiteral("second ; $() file.txt"));
        UStringVector paths;
        paths.Add(ToUString(first));
        paths.Add(ToUString(second));
        QtNativeProcessServices service;
        const UString parameters = ToUString(
            QString("--external-fixture \"%1\" --readonly \"parameter space\"").arg(output));
        IQtProcessHandle *process = nullptr;
        const HRESULT launched = service.Create(ToUString(program), parameters, paths, process);
        std::unique_ptr<IQtProcessHandle> owner(process);
        QCOMPARE(launched, S_OK);
        QVERIFY(owner);
        QTRY_VERIFY(QFileInfo(output).size() > 0);
        QFile result(output);
        QVERIFY(result.open(QIODevice::ReadOnly));
        const QJsonArray arguments = QJsonDocument::fromJson(result.readAll()).array();
        const QStringList expected = {"--readonly", "parameter space", first, second};
        QCOMPARE(arguments.size(), expected.size());
        for (qsizetype i = 0; i < expected.size(); ++i)
            QCOMPARE(arguments[i].toString().normalized(QString::NormalizationForm_C),
                     expected[i].normalized(QString::NormalizationForm_C));
        QTRY_COMPARE(owner->Status(), QtProcessStatus::Exited);
        const UString missing = ToUString(fixture.filePath("missing"));
        process = nullptr;
        const HRESULT failed = service.Create(missing, parameters, paths, process);
        std::unique_ptr<IQtProcessHandle> failedOwner(process);
        QVERIFY(failed != S_OK);
        QVERIFY(!failedOwner);
    }
    void associationAdapterPreservesPathAndReportsFailure_data() {
        QTest::addColumn<bool>("russian");
        QTest::newRow("en") << false;
        QTest::newRow("ru") << true;
    }
    void associationAdapterPreservesPathAndReportsFailure() {
        QFETCH(bool, russian);
        QTemporaryDir settings;
        const QString settingsPath = settings.filePath("language.ini");
        SaveRegLang(UString(russian ? "ru" : "-"), settingsPath);
        ReloadLang(settingsPath);
        const auto restoreLanguage = qScopeGuard([settingsPath] {
            SaveRegLang(UString("-"), settingsPath);
            ReloadLang(settingsPath);
        });
        const UString path(L"/папка с пробелом/имя <b>файла</b>.txt");
        QUrl seen;
        QtAssociatedFileServices success([&](const QUrl &url) {
            seen = url;
            return true;
        });
        QCOMPARE(success.StartApplication(UString(), path, nullptr), S_OK);
        QVERIFY(seen.isLocalFile());
        QCOMPARE(seen.toLocalFile(), ToQString(path));
        bool plain = false, geometry = false, reported = false;
        QString message;
        QTimer::singleShot(0, qApp, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) {
                plain = box->textFormat() == Qt::PlainText && box->text().contains(ToQString(path));
                message = box->text();
                auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                geometry = label && UiInspection::fullyVisible(*label, *box) &&
                           UiInspection::labelFits(*label);
                for (auto *button : box->buttons())
                    geometry = geometry && UiInspection::fullyVisible(*button, *box);
                reported =
                    UiInspection::writeReport(*box, russian ? "language-association-error-ru"
                                                            : "language-association-error-en");
                box->accept();
            }
        });
        QtAssociatedFileServices failure([](const QUrl &) -> bool { throw 1; });
        QCOMPARE(failure.StartApplication(UString(), path, nullptr), E_FAIL);
        QVERIFY(plain);
        QVERIFY(geometry && reported);
        QCOMPARE(message, (russian ? QStringLiteral("Не удалось открыть файл: %1")
                                   : QString("Could not open file: %1"))
                              .arg(ToQString(path)));
    }
    void associationWithoutHandlePreservesCopyAfterOwnerExit_data() {
        QTest::addColumn<bool>("changed");
        QTest::addColumn<bool>("launchFailed");
        QTest::addColumn<bool>("tryInternal");
        QTest::newRow("unchanged-preserve") << false << false << false;
        QTest::newRow("changed-preserve") << true << false << false;
        QTest::newRow("launch-failure-cleanup") << false << true << false;
        QTest::newRow("memory-probe-fallback") << false << false << true;
    }
    void associationWithoutHandlePreservesCopyAfterOwnerExit() {
        QFETCH(bool, changed);
        QFETCH(bool, launchFailed);
        QFETCH(bool, tryInternal);
        QTemporaryDir fixture;
        const QString name = QStringLiteral("файл с пробелом.bin");
        QVERIFY(write(fixture.filePath(name), "contents"));
        QProcess process;
        process.setWorkingDirectory(fixture.path());
        process.start(QStandardPaths::findExecutable("7z"), {"a", "-tzip", "original.zip", name});
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitCode(), 0);
        const QString archive = fixture.filePath("original.zip");
        ArchiveBackend backend;
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(backend);
        CAssociationService service;
        service.Result = launchFailed ? E_FAIL : S_OK;
        {
            CPanel panel;
            panel.AttachFolderManager(manager, fixture.filePath("settings.ini"));
            panel.AttachExternalFileServices(&service);
            COpenResult result;
            QCOMPARE(panel.BindToPath(ToUString(archive), UString(), result), S_OK);
            QCOMPARE(panel.GetFolder()->LoadItems(), S_OK);
            panel.OpenItemInArchive(0, tryInternal, true);
            QVERIFY(!service.Opened.isEmpty());
            QCOMPARE(QFile::exists(service.Opened), !launchFailed);
            QVERIFY(!backend.busy());
            if (changed)
                QVERIFY(write(service.Opened, "changed contents retained"));
        } // Native exit event + join, not an association's presumed completion.
        QCOMPARE(QFile::exists(service.Opened), !launchFailed);
        const auto parent = QFileInfo(service.Opened).absolutePath();
        if (!launchFailed) {
            QFile edited(service.Opened);
            QVERIFY(edited.open(QIODevice::ReadOnly));
            QCOMPARE(edited.readAll(),
                     changed ? QByteArray("changed contents retained") : QByteArray("contents"));
            edited.close();
            bool protectedCopy = false;
            for (const auto &entry : QtOwnedTempRegistry().Entries())
                if (entry.path == parent) {
                    protectedCopy = !entry.active && entry.externallyProtected;
                    QVERIFY(!QtOwnedTempRegistry().ClaimForDeletion(entry));
                }
            QVERIFY(protectedCopy);
            QVERIFY(QFile::remove(
                service.Opened)); // Fixture owns cleanup of intentionally retained data.
            QVERIFY(QDir().rmdir(parent));
        } else {
            QVERIFY(!QDir(parent).exists());
        }
    }
};
int main(int argc, char **argv) {
    if (argc >= 3 && QByteArray(argv[1]) == "--external-fixture") {
        QCoreApplication app(argc, argv);
        QFile output(app.arguments()[2]);
        if (!output.open(QIODevice::WriteOnly))
            return 1;
        const QByteArray bytes =
            QJsonDocument(QJsonArray::fromStringList(app.arguments().mid(3))).toJson();
        return output.write(bytes) == bytes.size() ? 0 : 2;
    }
    QApplication app(argc, argv);
    ExternalLaunchTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "external_launch_tests.moc"
