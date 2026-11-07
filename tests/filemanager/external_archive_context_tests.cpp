#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "7zip/UI/GUI/ExtractRes.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/resources/qtarchivedirectoryread.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "archive/resources/qtarchivewritelease.h"
#include "archive/support/native_archive_fixture.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/password/PasswordDialog.h"
#include "filemanager/external/qtexternalarchivetarget.h"
#include "filemanager/panels/Panel.h"
#include "localization/LangUtils.h"
#include "platform/temp/qtownedtempregistry.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>

using namespace SourceExtractFixture;

class ExternalArchiveContextTests final : public QObject {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static std::optional<UInt32> find(CPanel &panel, const QString &name) {
        for (UInt32 index = 0; index < panel.GetLoadedItemCount(); ++index)
            if (QDir::fromNativeSeparators(ToQString(panel.GetItemRelPath(index))) ==
                QDir::fromNativeSeparators(name))
                return index;
        return {};
    }
    static HRESULT load(CPanel &panel, bool flat = false) {
        auto *native = dynamic_cast<CAgentFolder *>(panel.GetFolder());
        if (!native)
            return E_FAIL;
        RINOK(ReadArchiveDirectory(*native, flat, panel.GetParent(), {}))
        if (panel.GetFlatMode() != flat)
            panel.ChangeFlatMode();
        return panel.LoadListItems();
    }
    static bool extract(const QString &archive, const QString &directory, const QString &password) {
        QProcess process;
        QStringList args{"x", "-y", "-o" + directory};
        if (!password.isNull())
            args.append("-p" + password);
        args.append({"--", archive});
        process.start(QStandardPaths::findExecutable("7z"), args);
        return process.waitForFinished(10000) && process.exitCode() == 0;
    }

    static void columns() {
        QTest::addColumn<int>("depth");
        QTest::addColumn<QString>("password");
        QTest::addColumn<bool>("changedSource");
        QTest::addColumn<int>("location");
        QTest::addColumn<bool>("blocked");
    }

  private slots:
    void updateSurvivesSourceDestruction_data() {
        columns();
        QTest::newRow("physical") << 0 << QString() << false << 0 << false;
        QTest::newRow("nested-file") << 1 << QString() << false << 0 << false;
        QTest::newRow("two-nested-files") << 2 << QString() << false << 0 << false;
        QTest::newRow("encrypted-nested")
            << 2 << QStringLiteral("fixture-password") << false << 0 << false;
        QTest::newRow("changed-physical") << 0 << QString() << true << 0 << false;
        QTest::newRow("changed-nested") << 2 << QString() << true << 0 << false;
        QTest::newRow("flat-subfolder") << 2 << QString() << false << 1 << false;
        QTest::newRow("inside-subfolder") << 2 << QString() << false << 2 << false;
        QTest::newRow("outer-write-busy") << 2 << QString() << false << 0 << true;
    }
    void updateSurvivesSourceDestruction() {
        QFETCH(int, depth);
        QFETCH(QString, password);
        QFETCH(bool, changedSource);
        QFETCH(int, location);
        QFETCH(bool, blocked);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name =
            (location ? QStringLiteral("folder/") : QString()) + QStringLiteral("данные 空.txt");
        QString archive = fixture.filePath("leaf.zip");
        QVERIFY(NativeArchiveFixture::Write(archive, {{"keep.bin", "keep"}, {name, "original"}},
                                            "zip", password));
        QStringList boundaries;
        for (int level = 0; level < depth; ++level) {
            const QString entry = QString("nested %1.zip").arg(level);
            const QString parent = fixture.filePath(QString("parent%1.zip").arg(level));
            QVERIFY(NativeArchiveFixture::Write(
                parent, {{entry, readFile(archive)}, {"other.bin", "other"}}, "zip", password));
            boundaries.prepend(entry);
            archive = parent;
        }
        auto host = std::make_unique<QWidget>();
        auto backend = std::make_unique<ArchiveBackend>();
        CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(*backend);
        auto source = std::make_unique<CPanel>();
        source->RebindWindowHost(host.get(), nullptr, nullptr);
        source->AttachFolderManager(manager, {});
        source->AttachReloadPresentation([](const CSelectedState &) { return S_OK; });
        source->SetArchiveOpenPassword(
            password.isNull() ? std::nullopt : std::optional(ArchivePassword(password)));
        COpenResult opened;
        QCOMPARE(source->BindToPath(ToUString(archive), UString(), opened), S_OK);
        for (const auto &entry : boundaries) {
            QCOMPARE(load(*source), S_OK);
            const auto index = find(*source, entry);
            QVERIFY(index);
            QCOMPARE(source->OpenItemInArchiveInternal(*index, nullptr, true), S_OK);
        }
        if (location == 2) {
            QCOMPARE(load(*source), S_OK);
            CMyComPtr<IFolderFolder> child;
            QCOMPARE(source->GetFolder()->BindToFolder(L"folder", &child), S_OK);
            QVERIFY(child);
            source->SetNewFolder(child);
        }
        QCOMPARE(load(*source, location == 1), S_OK);
        const QString selectedName = location == 2 ? QFileInfo(name).fileName() : name;
        const auto index = find(*source, selectedName);
        QVERIFY(index);
        CTempFileInfo temp;
        temp.FileIndex = *index;
        temp.RelPath = ToUString(selectedName);
        auto target = QtExternalArchiveTarget::Capture(*source, temp, *backend, {});
        QVERIFY(target);
        QPointer<CPanel> oldSource(source.get());
        QPointer<ArchiveBackend> oldBackend(backend.get());
        QPointer<QWidget> oldHost(host.get());
        source.reset();
        manager.Release();
        backend.reset();
        host.reset();
        QVERIFY(!oldSource && !oldBackend && !oldHost);
        const QString replacement = fixture.filePath("replacement bytes.txt");
        const QByteArray bytes("updated without the source window");
        QVERIFY(writeFile(replacement, bytes));
        if (changedSource) {
            QVERIFY(QFile::remove(archive));
            QVERIFY(
                NativeArchiveFixture::Write(archive, {{"new.txt", "another generation"}}, "zip"));
        }
        const QByteArray before = readFile(archive);
        std::optional<QtArchiveWriteLease> busy;
        if (blocked) {
            auto lease = QtArchiveWriteLease::Acquire({archive});
            QVERIFY(lease);
            busy.emplace(std::move(*lease));
        }
        QTimer responder;
        dialogs_.connectResponder(responder, this, [&] {
            auto *modal = dialogs_.activeDialog();
            if (auto *question = dynamic_cast<CPasswordDialog *>(modal)) {
                question->findChild<QLineEdit *>("passwordEdit")->setText(password);
                question->accept();
            } else if (auto *box = qobject_cast<QMessageBox *>(modal))
                box->accept();
            else if (auto *progress = dynamic_cast<CProgressDialog *>(modal)) {
                auto *close = progress->findChild<QPushButton *>("IDCANCEL");
                if (close && close->text() == ToQString(LangString(IDS_CLOSE)))
                    close->click();
            }
        });
        responder.start(1);
        const auto result = target->Update(replacement, nullptr);
        if (changedSource) {
            QVERIFY(result.Result != S_OK);
            QCOMPARE(result.Published, std::optional<bool>(false));
            QCOMPARE(readFile(archive), before);
            QCOMPARE(readFile(replacement), bytes);
            return;
        }
        if (blocked) {
            QVERIFY(result.Result != S_OK);
            QCOMPARE(result.Published, std::optional<bool>(false));
            QCOMPARE(readFile(archive), before);
            QCOMPARE(result.retainedInputs.size(), depth);
            const auto recovered = fixture.filePath("retained");
            QVERIFY(extract(result.retainedInputs.last(), recovered, password));
            QCOMPARE(readFile(QDir(recovered).filePath(name)), bytes);
            for (const auto &input : result.retainedInputs) {
                QVERIFY(QFileInfo::exists(input));
                const auto directory = QFileInfo(input).absolutePath();
                QtOwnedTempRegistry registry;
                bool removed = false;
                for (const auto &entry : registry.Entries()) {
                    if (entry.path != directory)
                        continue;
                    auto claim = registry.ClaimForDeletion(entry);
                    QVERIFY(claim);
                    removed = QDir(directory).removeRecursively();
                }
                QVERIFY(removed);
            }
        } else {
            QCOMPARE(result.Result, S_OK);
            QCOMPARE(result.Published, std::optional<bool>(true));
            QVERIFY(result.retainedInputs.isEmpty());
        }
        QString current = archive;
        for (int level = 0; level <= depth; ++level) {
            const QString output = fixture.filePath(QString("output%1").arg(level));
            QVERIFY(extract(current, output, password));
            if (level == depth) {
                QCOMPARE(readFile(QDir(output).filePath(name)),
                         blocked ? QByteArray("original") : bytes);
                QCOMPARE(readFile(QDir(output).filePath("keep.bin")), QByteArray("keep"));
            } else {
                QCOMPARE(readFile(QDir(output).filePath("other.bin")), QByteArray("other"));
                current = QDir(output).filePath(boundaries[level]);
            }
        }
        QCOMPARE(readFile(replacement), bytes);
    }
};

QTEST_MAIN(ExternalArchiveContextTests)
#include "external_archive_context_tests.moc"
