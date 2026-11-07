#include "7zip/PropID.h"
#include "archive/agent/Agent.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/panel_load_folder.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "platform/support/external_editor_fixture.h"
#include "support/progress_dialog.h"
#include "ui/support/layout_inspection.h"

namespace {
// Real source filesystem operations with a deterministic nested GUI callback.
class RenameFolder final : public IFolderFolder,
                           public IFolderOperations,
                           public IFolderWasChanged,
                           public CMyUnknownImp {
    CMyComPtr<IFolderFolder> folder_;
    CMyComPtr<IFolderOperations> operations_;
    Z7_COM_UNKNOWN_IMP_3(IFolderFolder, IFolderOperations, IFolderWasChanged)
  public:
    std::function<void()> nestedCallback;
    HRESULT result = S_OK;
    unsigned changeQueries = 0;
    explicit RenameFolder(const QString &path) : folder_(new NFsFolder::CFSFolder(path)) {
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
    Z7_COM7F_IMF(GetPropertyInfo(UInt32 index, BSTR *name, PROPID *id, VARTYPE *type)) override {
        return folder_->GetPropertyInfo(index, name, id, type);
    }
    Z7_COM7F_IMF(GetFolderProperty(PROPID id, PROPVARIANT *value)) override {
        return folder_->GetFolderProperty(id, value);
    }
    Z7_COM7F_IMF(WasChanged(Int32 *changed)) override {
        ++changeQueries;
        *changed = 1;
        return S_OK;
    }
    Z7_COM7F_IMF(Rename(UInt32 index, const wchar_t *name, IProgress *progress)) override {
        nestedCallback();
        return result == S_OK ? operations_->Rename(index, name, progress) : result;
    }
    Z7_COM7F_IMF(CreateFolder(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(CreateFile(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(Delete(const UInt32 *, UInt32, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(CopyTo(Int32, const UInt32 *, UInt32, Int32, Int32, const wchar_t *,
                        IFolderOperationsExtractCallback *))
    override { return E_NOTIMPL; }
    Z7_COM7F_IMF(CopyFrom(Int32, const wchar_t *, const wchar_t *const *, UInt32, IProgress *))
    override { return E_NOTIMPL; }
    Z7_COM7F_IMF(SetProperty(UInt32, PROPID, const PROPVARIANT *, IProgress *)) override {
        return E_NOTIMPL;
    }
    Z7_COM7F_IMF(CopyFromFile(UInt32, const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
};
} // namespace

class PanelNameTextTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
  private slots:
    void renameSuppressesNestedCallbacks_data() {
        QTest::addColumn<HRESULT>("result");
        QTest::addColumn<bool>("outer");
        for (const HRESULT result : {S_OK, E_INVALIDARG, E_ABORT})
            for (bool outer : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(quint32(result)).arg(outer)))
                    << result << outer;
    }
    void renameSuppressesNestedCallbacks() {
        QFETCH(HRESULT, result);
        QFETCH(bool, outer);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("original 文件.txt");
        const QString renamed = QStringLiteral("renamed δ.txt");
        QVERIFY(ExternalEditorFixture::write(fixture.filePath(original), "control"));
        auto *spec = new RenameFolder(fixture.path());
        CMyComPtr<IFolderFolder> folder = spec;
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        panel.Set_AutoRefresh_Mode(true);
        unsigned reloads = 0;
        panel.AttachReloadPresentation([&](const CSelectedState &) {
            ++reloads;
            return S_OK;
        });
        bool callbackSeen = false;
        bool notifications = true;
        unsigned nestedReloads = 99;
        unsigned nestedQueries = 99;
        spec->result = result;
        spec->nestedCallback = [&] {
            QMetaObject::invokeMethod(
                &panel,
                [&] {
                    callbackSeen = true;
                    notifications = panel.ShouldProcessNotifications();
                    panel.OnTimer();
                    nestedReloads = reloads;
                    nestedQueries = spec->changeQueries;
                },
                Qt::BlockingQueuedConnection);
        };
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &panel, [] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (TestProgressDialog::DismissVisibleResult(widget))
                    continue;
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    box->accept();
            }
        });
        closeError.start(2);
        std::unique_ptr<CPanel::CDisableNotify> notify;
        std::unique_ptr<CPanel::CDisableTimerProcessing> timer;
        if (outer) {
            notify = std::make_unique<CPanel::CDisableNotify>(panel);
            timer = std::make_unique<CPanel::CDisableTimerProcessing>(panel);
        }
        QCOMPARE(panel.OnEndLabelEdit(0, ToUString(renamed)), result);
        closeError.stop();
        QVERIFY(callbackSeen);
        QVERIFY2(!notifications && nestedReloads == 0 && nestedQueries == 0,
                 qPrintable(QString("notify=%1 reloads=%2 changeQueries=%3")
                                .arg(notifications)
                                .arg(nestedReloads)
                                .arg(nestedQueries)));
        QCOMPARE(panel.ShouldProcessNotifications(), !outer);
        panel.OnTimer();
        QCOMPARE(reloads, outer ? 0u : 1u);
        timer.reset();
        notify.reset();
        QVERIFY(panel.ShouldProcessNotifications());
        panel.OnTimer();
        QCOMPARE(reloads, outer ? 1u : 2u);
        QCOMPARE(QFileInfo::exists(fixture.filePath(original)), result != S_OK);
        QFile payload(fixture.filePath(result == S_OK ? renamed : original));
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("control"));
    }
    void sourceNames_data() {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("shown");
        QTest::newRow("ordinary") << QStringLiteral("name 文件.txt")
                                  << QStringLiteral("name 文件.txt");
        QTest::newRow("rlo") << QString("report") + QChar(0x202E) + "txt.exe"
                             << QString("report_txt.exe");
        QTest::newRow("four-spaces") << QString("a    b") << QString("a    b");
        QTest::newRow("five-spaces") << QString("a     b") << QString("a ... b");
        QTest::newRow("many-spaces") << QString("a          b     c") << QString("a ... b ... c");
        QTest::newRow("empty") << QString() << QString("_");
#ifdef _WIN32
        QTest::newRow("trailing") << QString("name ") << QString("name ") + QChar(0x9C);
#else
        QTest::newRow("trailing") << QString("name ") << QString("name ");
#endif
    }
    void sourceNames() {
        QFETCH(QString, raw);
        QFETCH(QString, shown);
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->firstName = ToUString(raw);
        spec->fastNames = true;
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        CPropColumn property{};
        property.ID = kpidName;
        QCOMPARE(panel.GetItemText(0, property), shown);
        QCOMPARE(ToQString(panel.GetItemName(0)), raw);
        spec->nameResult = E_ABORT;
        QCOMPARE(panel.GetItemText(0, property), shown);
        property.ID = kpidPrefix;
        spec->prefix = L"folder 文件/";
        spec->prefixResult = E_FAIL;
        QCOMPARE(panel.GetItemText(0, property), QStringLiteral("folder 文件/"));
    }
    void fallbackPropertyErrorsStayInCell() {
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->firstName = L"first\r\nname";
        CPanel panel(folder);
        QCOMPARE(panel.LoadListItems(), S_OK);
        CPropColumn property{};
        property.ID = kpidName;
        QCOMPARE(panel.GetItemText(0, property), QString("first  name"));
        spec->nameResult = E_ABORT;
        QCOMPARE(panel.GetItemText(0, property), QString("Error: "));
        property.ID = kpidPrefix;
        spec->prefixResult = E_FAIL;
        QCOMPARE(panel.GetItemText(0, property), QString("Error: "));
    }
    void nameCallbackKeepsEditingAndIndices_data() { sourceNames_data(); }
    void nameCallbackKeepsEditingAndIndices() {
        QFETCH(QString, raw);
        QFETCH(QString, shown);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend backend;
        MainWindow window(&backend);
        action(window, "openRootAction")->trigger();
        auto *view = static_cast<FileTable *>(table(window));
        auto *spec = new LoadFolder;
        CMyComPtr<IFolderFolder> folder = spec;
        spec->fastNames = true;
        spec->firstName = ToUString(raw);
        view->sourcePanel->SetNewFolder(folder);
        QCOMPARE(view->sourcePanel->RefreshListCtrl_SaveFocused(), S_OK);
        QPersistentModelIndex item;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == 0)
                item = view->model()->index(row, 0);
        QVERIFY(item.isValid());
        QCOMPARE(item.data().toString(), shown);
        QCOMPARE(item.data(Qt::EditRole).toString(), raw.normalized(QString::NormalizationForm_C));
        QCOMPARE(item.data(Qt::UserRole + 3).toString(), raw);
        QCOMPARE(item.data(Qt::UserRole + 6).toUInt(), 0u);
        {
            CPanel::CDisableNotify guard(*view->sourcePanel);
            SetPanelData(item, QVariant(), Qt::DisplayRole);
            QCOMPARE(item.data().toString(), QString());
            QCOMPARE(item.data(Qt::EditRole).toString(),
                     raw.normalized(QString::NormalizationForm_C));
        }
        QCOMPARE(item.data().toString(), shown);
        window.show();
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*view, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("source-name-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
    }
};

QTEST_MAIN(PanelNameTextTests)
#include "panel_name_text_tests.moc"
