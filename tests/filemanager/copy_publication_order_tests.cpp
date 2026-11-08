#include "Windows/PropVariant.h"
#include "dialogs/copy/alias.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include <QLineEdit>

namespace {
// Use original filesystem byte operations behind the stream-route interface.
// Only the terminal result/type is controlled; this is not native ADS evidence.
class CopyPhaseFolder final : public IFolderFolder, public IFolderOperations, public CMyUnknownImp {
    CMyComPtr<IFolderFolder> folder_;
    CMyComPtr<IFolderOperations> operations_;
    Z7_COM_UNKNOWN_IMP_2(IFolderFolder, IFolderOperations)
  public:
    HRESULT terminal = S_OK;
    explicit CopyPhaseFolder(const QString &path) : folder_(new NFsFolder::CFSFolder(path)) {
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
        if (id == kpidType) {
            NWindows::NCOM::CPropVariant type(L"AltStreamsFolder");
            return type.Detach(value);
        }
        return folder_->GetFolderProperty(id, value);
    }
    Z7_COM7F_IMF(CreateFolder(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(CreateFile(const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(Rename(UInt32, const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(Delete(const UInt32 *, UInt32, IProgress *)) override { return E_NOTIMPL; }
    Z7_COM7F_IMF(CopyTo(Int32 move, const UInt32 *indices, UInt32 count, Int32 streams,
                        Int32 replace, const wchar_t *path,
                        IFolderOperationsExtractCallback *callback))
    override {
        const HRESULT result =
            operations_->CopyTo(move, indices, count, streams, replace, path, callback);
        return result == S_OK ? terminal : result;
    }
    Z7_COM7F_IMF(CopyFrom(Int32, const wchar_t *, const wchar_t *const *, UInt32, IProgress *))
    override { return E_NOTIMPL; }
    Z7_COM7F_IMF(SetProperty(UInt32, PROPID, const PROPVARIANT *, IProgress *)) override {
        return E_NOTIMPL;
    }
    Z7_COM7F_IMF(CopyFromFile(UInt32, const wchar_t *, IProgress *)) override { return E_NOTIMPL; }
};
} // namespace

class CopyPublicationOrderTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        address->setFocus();
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
    static void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void destinationRefreshKeepsNewAddressInput() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("source");
        const QString output = fixture.filePath("destination");
        QVERIFY(QDir().mkpath(input) && QDir().mkpath(output));
        write(input + "/payload.txt", "copied bytes");
        for (int index = 0; index < 301; ++index)
            write(QDir(output).filePath(QString("existing-%1.txt").arg(index)), "existing");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, output);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 301);
        auto *source = openPanel(window, 0, input);
        QTRY_COMPARE(source->rowCount(), 1);
        source->selectRow(0);
        source->setFocus();
        auto *address =
            window.findChild<QWidget *>("filePanel1")->findChild<QLineEdit *>("archivePath");
        bool scheduled = false;
        bool edited = false;
        connect(peer->model(), &QAbstractItemModel::rowsInserted, &window, [&] {
            if (scheduled || peer->isEnabled())
                return;
            scheduled = true;
            QTimer::singleShot(0, &window, [&] {
                PanelKeys::focusAddress(address);
                address->selectAll();
                QTest::keyClicks(address, "new draft");
                edited = true;
            });
        });
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = window.findChild<CopyDialog *>("copyDialog");
            QVERIFY(dialog && dialog->isVisible());
            dialog->accept();
        });
        action(window, "copyAction")->trigger();
        QTRY_VERIFY(edited && peer->isEnabled() && peer->rowCount() == 302);
        QCOMPARE(read(output + "/payload.txt"), QByteArray("copied bytes"));
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(address->hasFocus());
        QCOMPARE(address->text(), QString("new draft"));
        QVERIFY(source->selectionModel()->selectedRows().isEmpty());
    }
    void nativeCopyPhases_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("move");
        QTest::addColumn<HRESULT>("result");
        for (unsigned pane : {0u, 1u})
            for (bool move : {false, true})
                for (HRESULT result : {S_OK, E_INVALIDARG, E_ABORT})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(move).arg(quint32(result))))
                        << pane << move << result;
    }
    void nativeCopyPhases() {
        QFETCH(unsigned, pane);
        QFETCH(bool, move);
        QFETCH(HRESULT, result);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString input = fixture.filePath("source 文件"), output = fixture.filePath("peer δ");
        QVERIFY(QDir().mkpath(input) && QDir().mkpath(output));
        const QString name = QStringLiteral("payload 空.bin");
        const QByteArray bytes("phase\0bytes", 11);
        write(QDir(input).filePath(name), bytes);
        write(QDir(input).filePath("control.txt"), "source control");
        write(QDir(output).filePath("seed.txt"), "peer control");
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(WindowActivation::waitForActive(window));
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, output);
        QTRY_COMPARE(peer->rowCount(), 1);
        auto *source = openPanel(window, pane, input);
        QTRY_COMPARE(source->rowCount(), 2);
        auto *spec = new CopyPhaseFolder(input);
        spec->terminal = result;
        CMyComPtr<IFolderFolder> folder = spec;
        source->sourcePanel->SetNewFolder(folder);
        source->sourcePanel->OnReload();
        QTRY_COMPARE(source->rowCount(), 2);
        source->sourcePanel->Set_AutoRefresh_Mode(false);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        int payload = -1;
        for (int row = 0; row < source->rowCount(); ++row)
            if (source->model()->index(row, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name)
                payload = row;
        QVERIFY(payload >= 0);
        source->selectRow(payload);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        bool submitted = false;
        QStringList errors;
        QStringList errorTitles;
        QList<QMessageBox::Icon> errorIcons;
        unsigned callerErrors = 0;
        QList<int> peerRowsAtError;
        QList<int> sourceRowsAtError;
        QTimer driver;
        dialogs_.connectResponder(driver, &window, [&] {
            if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                dialog && dialog->isVisible() && !submitted) {
                submitted = true;
                dialog->findChild<QLineEdit *>("copyDestination")->setText(output + '/');
                dialog->accept();
            }
            if (auto *report = TestProgressDialog::ResultWindow(dialogs_.activeDialog())) {
                const QString diagnostic = TestProgressDialog::ReportText(*report);
                if (!diagnostic.isEmpty()) {
                    errors << diagnostic;
                    peerRowsAtError << peer->rowCount();
                    sourceRowsAtError << source->rowCount();
                }
                TestProgressDialog::CloseResult(*report);
                return;
            }
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && dialogs_.ready(*box)) {
                    errors << box->text();
                    errorTitles << box->windowTitle();
                    errorIcons << box->icon();
                    if (box->objectName() == "panelArchiveError") {
                        ++callerErrors;
                    }
                    peerRowsAtError << peer->rowCount();
                    sourceRowsAtError << source->rowCount();
                    box->accept();
                }
        });
        driver.start(0);
        QTest::keyClick(source, move ? Qt::Key_F6 : Qt::Key_F5);
        QVERIFY(submitted);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        QTRY_COMPARE(peer->rowCount(), 2);
        driver.stop();
        // Original worker progress reports its failure, then OnCopy presents
        // the returned HRESULT. Preserve both source-owned diagnostic phases.
        QCOMPARE(errors.size(), result == E_INVALIDARG ? 2 : 0);
        for (const QString &error : errors)
            QCOMPARE(error, ToQString(HResultToMessage(result)));
        for (const QString &title : errorTitles)
#ifdef Q_OS_MACOS
            QCOMPARE(title, QString());
#else
            QCOMPARE(title, QString("7-Zip"));
#endif
        for (QMessageBox::Icon icon : errorIcons)
            QCOMPARE(icon, QMessageBox::Critical);
        QCOMPARE(callerErrors, result == E_INVALIDARG ? 1u : 0u);
        for (int rows : peerRowsAtError)
            QCOMPARE(rows, 1);
        for (int rows : sourceRowsAtError)
            QCOMPARE(rows, 2);
        QCOMPARE(read(QDir(output).filePath(name)), bytes);
        QCOMPARE(QFileInfo::exists(QDir(input).filePath(name)), !move);
        if (!move)
            QCOMPARE(read(QDir(input).filePath(name)), bytes);
        QCOMPARE(read(QDir(input).filePath("control.txt")), QByteArray("source control"));
        QCOMPARE(read(QDir(output).filePath("seed.txt")), QByteArray("peer control"));
        QCOMPARE(source->rowCount(), move ? 1 : 2);
        QVERIFY(source->selectionModel()->selectedRows().isEmpty());
        QVERIFY(source->sourcePanel->ShouldProcessNotifications());
        QVERIFY(peer->sourcePanel->ShouldProcessNotifications());
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(source->hasFocus());
        QCOMPARE(source->sourcePanel->GetFolder(), folder.Interface());
    }
};

QTEST_MAIN(CopyPublicationOrderTests)
#include "copy_publication_order_tests.moc"
