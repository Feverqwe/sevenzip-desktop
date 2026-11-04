#include "7zip/UI/FileManager/resource.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashservice.h"
#include "settings/RegistryUtils.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>

class SourceDeleteTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    class Trash final : public IQtTrashService {
      public:
        int calls = 0;
        int failAfter = 0;
        std::function<void()> during;
        bool MoveToTrash(const QString &path) override {
            ++calls;
            const bool removed = (failAfter < 0 || calls <= failAfter) && QFile::remove(path);
            if (during)
                during();
            return removed;
        }
    };
    // Delegate actual bytes to the original provider; inject only the terminal
    // result after one entry to make partial failure/Cancel deterministic.
    class DeleteFolder final : public IFolderFolder,
                               public IFolderOperations,
                               public CMyUnknownImp {
        CMyComPtr<IFolderFolder> folder_;
        CMyComPtr<IFolderOperations> operations_;
        Z7_COM_UNKNOWN_IMP_2(IFolderFolder, IFolderOperations)
      public:
        HRESULT terminal = S_OK;
        std::function<void()> during;
        explicit DeleteFolder(const QString &path) : folder_(new NFsFolder::CFSFolder(path)) {
            folder_.QueryInterface(IID_IFolderOperations, &operations_);
        }
        Z7_COM7F_IMF(LoadItems()) override { return folder_->LoadItems(); }
        Z7_COM7F_IMF(GetNumberOfItems(UInt32 *count)) override {
            return folder_->GetNumberOfItems(count);
        }
        Z7_COM7F_IMF(GetProperty(UInt32 index, PROPID id, PROPVARIANT *value)) override {
            return folder_->GetProperty(index, id, value);
        }
        Z7_COM7F_IMF(BindToFolder(UInt32 index, IFolderFolder **folder)) override {
            return folder_->BindToFolder(index, folder);
        }
        Z7_COM7F_IMF(BindToFolder(const wchar_t *name, IFolderFolder **folder)) override {
            return folder_->BindToFolder(name, folder);
        }
        Z7_COM7F_IMF(BindToParentFolder(IFolderFolder **folder)) override {
            return folder_->BindToParentFolder(folder);
        }
        Z7_COM7F_IMF(GetNumberOfProperties(UInt32 *count)) override {
            return folder_->GetNumberOfProperties(count);
        }
        Z7_COM7F_IMF(GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type))
        override { return folder_->GetPropertyInfo(index, name, id, type); }
        Z7_COM7F_IMF(GetFolderProperty(PROPID id, PROPVARIANT *value)) override {
            if (id == kpidType) {
                NWindows::NCOM::CPropVariant type(L"RootFolder");
                return type.Detach(value);
            }
            return folder_->GetFolderProperty(id, value);
        }
        Z7_COM7F_IMF(CreateFolder(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
        Z7_COM7F_IMF(CreateFile(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
        Z7_COM7F_IMF(Rename(UInt32, const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
        Z7_COM7F_IMF(CopyTo(Int32, const UInt32 *, UInt32, Int32, Int32, const wchar_t *,
                            IFolderOperationsExtractCallback *))
        override { return E_NOTIMPL; }
        Z7_COM7F_IMF(CopyFrom(Int32, const wchar_t *, const wchar_t *const *, UInt32, IProgress *))
        override { return E_NOTIMPL; }
        Z7_COM7F_IMF(SetProperty(UInt32, PROPID, const PROPVARIANT *, IProgress *)) override {
            return E_NOTIMPL;
        }
        Z7_COM7F_IMF(CopyFromFile(UInt32, const wchar_t *, IProgress *)) override {
            return E_NOTIMPL;
        }
        Z7_COM7F_IMF(Delete(const UInt32 *indices, UInt32 count, IProgress *progress)) override {
            during();
            const HRESULT result =
                operations_->Delete(indices, terminal == S_OK ? count : 1, progress);
            return result == S_OK ? terminal : result;
        }
    };
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
  private slots:
    void trashKeepsPreparedPathsAcrossProviderReload() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const QString &name : {QStringLiteral("a 空.bin"), QStringLiteral("b space.bin"),
                                    QStringLiteral("keep.bin")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected bytes"), qint64(15));
        }
        ControlledBackend backend;
        Trash trash;
        trash.failAfter = -1;
        MainWindow window(&backend, {}, &trash);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 3);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        CRecordVector<UInt32> indices;
        indices.Add(0);
        indices.Add(1);
        const QStringList selected{ToQString(panel.GetItemName(0)),
                                   ToQString(panel.GetItemName(1))};
        const QString kept = ToQString(panel.GetItemName(2));
        HRESULT reload = E_FAIL;
        trash.during = [&] {
            if (trash.calls == 1)
                reload = folder->LoadItems();
        };
        CSelectedState state;
        QCOMPARE(panel.DeleteItems(true, trash, state, &indices, false).Result, S_OK);
        QCOMPARE(reload, S_OK);
        QCOMPARE(trash.calls, 2);
        for (const QString &name : selected)
            QVERIFY(!QFileInfo::exists(fixture.filePath(name)));
        QFile protectedFile(fixture.filePath(kept));
        QVERIFY(protectedFile.open(QIODevice::ReadOnly));
        QCOMPARE(protectedFile.readAll(), QByteArray("protected bytes"));
    }
    void refusedConfirmation_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("cancel");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool cancel : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(cancel)))
                        << pane << language << cancel;
    }
    void refusedConfirmation() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(bool, cancel);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture, peerDirectory;
        QVERIFY(fixture.isValid());
        QVERIFY(peerDirectory.isValid());
        const QString name = QStringLiteral("keep 空 with spaces.bin");
        QFile keep(fixture.filePath(name));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        QCOMPARE(keep.write("protected bytes"), qint64(15));
        keep.close();
        ControlledBackend backend;
        Trash trash;
        MainWindow window(&backend, {}, &trash);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        QTRY_COMPARE(view->rowCount(), 1);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        view->setFocus();
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        const auto selection = view->selectionModel()->selection();
        bool observed = false;
        bool sourceDefault = false;
        QString title, message;
        bool reports = true;
        QTimer refusal;
        dialogs_.connectResponder(refusal, &window, [&] {
            auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog());
            if (!box)
                return;
            refusal.stop();
            observed = true;
            sourceDefault = box->defaultButton() == box->button(QMessageBox::Yes);
            title = box->windowTitle();
            message = box->text();
            for (const QSize size : {QSize(400, 200), QSize(300, 180)}) {
                box->resize(size);
                QApplication::processEvents();
                reports &= UiInspection::writeReport(*box, QString("source-delete-refused-%1-%2")
                                                               .arg(QTest::currentDataTag())
                                                               .arg(size.width()));
                for (auto *button : box->buttons())
                    reports &= UiInspection::fullyVisible(*button, *box);
            }
            QFile external(fixture.filePath("external.bin"));
            if (external.open(QIODevice::WriteOnly))
                external.write("external bytes");
            box->button(cancel ? QMessageBox::Cancel : QMessageBox::No)->click();
        });
        refusal.start(5);
#ifdef Q_OS_MACOS
        PanelKeys::command(view, Qt::Key_Backspace, Qt::ControlModifier);
#else
        action(window, "deleteAction")->trigger();
#endif
        refusal.stop();
        QVERIFY(observed);
        QVERIFY(sourceDefault);
#ifndef Q_OS_MACOS
        QCOMPARE(title, ToQString(LangString(IDS_CONFIRM_FILE_DELETE)));
#endif
        QVERIFY(message.contains(name));
        QVERIFY(reports);
        // Original Delete returns before RefreshListCtrl on No or Cancel.
        QCOMPARE(view->rowCount(), 1);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QCOMPARE(view->selectionModel()->selection(), selection);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(trash.calls, 0);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peer->rowCount(), 0);
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
        QVERIFY(QFileInfo::exists(fixture.filePath("external.bin")));
    }
    void accepted_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("mode");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (int mode : {0, 1, 2, 3, 4})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(mode)))
                        << pane << language << mode;
    }
    void accepted() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(int, mode);
        const bool recycle = mode >= 3;
        const bool partial = mode == 1 || mode == 2 || mode == 4;
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture, peerDirectory;
        QVERIFY(fixture.isValid());
        QVERIFY(peerDirectory.isValid());
        for (const QString name : {QStringLiteral("a 空.bin"), QStringLiteral("b space.bin"),
                                   QStringLiteral("keep.bin")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected bytes"), qint64(15));
        }
        ControlledBackend backend;
        Trash trash;
        trash.failAfter = partial ? 1 : -1;
        MainWindow window(&backend, {}, &trash);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        QTRY_COMPARE(view->rowCount(), 3);
        auto &panel = *view->sourcePanel;
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        bool workerSeen = false;
        bool workerNotifications = true;
        if (mode == 1 || mode == 2) {
            auto *spec = new DeleteFolder(fixture.path());
            folder = spec;
            spec->terminal = mode == 1 ? E_FAIL : E_ABORT;
            spec->during = [&] {
                QMetaObject::invokeMethod(
                    &window,
                    [&] {
                        workerSeen = true;
                        workerNotifications = panel.ShouldProcessNotifications();
                    },
                    Qt::BlockingQueuedConnection);
            };
            // Generic source projection avoids pretending a forwarding
            // fixture is the concrete filesystem provider used by the UI.
            panel.SetNewFolder(folder);
            panel.OnReload();
            QTRY_COMPARE(view->rowCount(), 3);
        }
        panel.Set_AutoRefresh_Mode(false);
        const auto itemNamed = [view](const QString &name) {
            const auto matches = view->model()->match(view->model()->index(0, 0), Qt::DisplayRole,
                                                      name, 1, Qt::MatchExactly);
            return matches.isEmpty() ? QModelIndex{} : matches.front();
        };
        const QModelIndex first = itemNamed(QStringLiteral("a 空.bin"));
        const QModelIndex second = itemNamed(QStringLiteral("b space.bin"));
        QVERIFY(first.isValid());
        QVERIFY(second.isValid());
        view->setCurrentCell(first.row(), 0);
        view->selectionModel()->select(first, QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
        view->selectionModel()->select(second,
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        view->setFocus();
        CRecordVector<UInt32> operated;
        panel.Get_ItemIndices_Operated(operated);
        QCOMPARE(operated.Size(), 2u);
        const QString firstDeleted = ToQString(panel.GetItemRelPath2(operated[0]));
        const QString secondDeleted = ToQString(panel.GetItemRelPath2(operated[1]));
        int confirmations = 0;
        int errors = 0;
        bool reports = true;
        QString errorTitle;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog());
            if (!box)
                return;
            if (auto *yes = box->button(QMessageBox::Yes)) {
                ++confirmations;
                yes->click();
            } else {
                ++errors;
                errorTitle = box->windowTitle();
#ifdef Q_OS_MACOS
                if (recycle)
                    errorTitle = box->text();
                else if (auto *progress = dynamic_cast<CProgressDialog *>(box->parentWidget()))
                    errorTitle = ToQString(progress->Sync.FinalMessage.ErrorMessage.Title);
#endif
                for (const QSize size : {QSize(400, 200), QSize(300, 180)}) {
                    box->resize(size);
                    QApplication::processEvents();
                    reports &= UiInspection::writeReport(*box, QString("source-delete-error-%1-%2")
                                                                   .arg(QTest::currentDataTag())
                                                                   .arg(size.width()));
                    for (auto *button : box->buttons())
                        reports &= UiInspection::fullyVisible(*button, *box);
                }
                box->accept();
            }
        });
        responder.start(1);
        if (mode == 1 || mode == 2) {
            CSelectedState selection;
            QCOMPARE(panel.DeleteItems(false, trash, selection).Result,
                     mode == 1 ? E_FAIL : E_ABORT);
        } else if (recycle)
            action(window, "deleteAction")->trigger();
        else {
            PanelKeys::deletePermanently(view, action(window, "deleteAction"));
            QTest::keyRelease(view, Qt::Key_Shift);
        }
        responder.stop();
        QCOMPARE(confirmations, 1);
        QCOMPARE(errors, mode == 1 || mode == 4 ? 1 : 0);
        QVERIFY(reports);
        if (errors)
            QCOMPARE(errorTitle, ToQString(LangString(IDS_ERROR_DELETING)));
        QTRY_COMPARE(view->rowCount(), partial ? 2 : 1);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(workerSeen, mode == 1 || mode == 2);
        if (workerSeen)
            QVERIFY(!workerNotifications);
        QCOMPARE(trash.calls, recycle ? 2 : 0);
        QVERIFY(!QFileInfo::exists(fixture.filePath(firstDeleted)));
        QCOMPARE(QFileInfo::exists(fixture.filePath(secondDeleted)), partial);
        if (partial) {
            QFile remaining(fixture.filePath(secondDeleted));
            QVERIFY(remaining.open(QIODevice::ReadOnly));
            QCOMPARE(remaining.readAll(), QByteArray("protected bytes"));
        }
        QFile keep(fixture.filePath("keep.bin"));
        QVERIFY2(keep.open(QIODevice::ReadOnly),
                 qPrintable(
                     QStringLiteral("Operated entries: %1, %2").arg(firstDeleted, secondDeleted)));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peer->rowCount(), 0);
    }
    void deferredRefresh_data() {
        QTest::addColumn<bool>("recycle");
        QTest::newRow("permanent") << false;
        QTest::newRow("trash") << true;
    }
    void deferredRefresh() {
        QFETCH(bool, recycle);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (int i = 0; i < 258; ++i) {
            QFile file(fixture.filePath(QString("entry-%1.bin").arg(i)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected bytes"), qint64(15));
        }
        ControlledBackend backend;
        Trash trash;
        trash.failAfter = -1;
        MainWindow window(&backend, {}, &trash);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 258);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        view->selectRow(0);
        view->setCurrentCell(0, 0);
        view->setFocus();
        const QString removed = view->model()->index(0, 0).data(Qt::UserRole + 3).toString();
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog())) {
                answer.stop();
                QTest::keyClick(box, Qt::Key_Return);
            }
        });
        answer.start(5);
        if (recycle)
            action(window, "deleteAction")->trigger();
        else {
            PanelKeys::deletePermanently(view, action(window, "deleteAction"));
            QTest::keyRelease(view, Qt::Key_Shift);
        }
        answer.stop();
        QVERIFY(!panel.ShouldProcessNotifications());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 257);
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(!QFileInfo::exists(fixture.filePath(removed)));
        QCOMPARE(view->model()->index(view->currentRow(), 0).data().toString(),
                 view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString());
        QVERIFY(!view->model()->index(view->currentRow(), 0).data().toString().isEmpty());
        QFile keep(fixture.filePath("entry-257.bin"));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceDeleteTests)
#include "source_delete_tests.moc"
