#include "7zip/UI/FileManager/resource.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "fileops/links/qtlinkservices.h"
#include "localization/LangUtils.h"
#include "platform/support/native_link_fixture.h"
#include "platform/trash/qttrashoperation.h"
#include "platform/trash/qttrashservice.h"
#include "settings/RegistryUtils.h"
#include "support/delete_reply.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "support/widget_dialogs.h"
#include "support/window_activation.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QClipboard>
#include <QScopeGuard>
#include <QSemaphore>

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
            const QFileInfo item(path);
            const bool removed = (failAfter < 0 || calls <= failAfter) &&
                                 (item.isSymLink() && item.isDir() ? QtRemoveFileSystemEntry(path)
                                                                   : QFile::remove(path));
            if (during)
                during();
            return removed;
        }
    };
    static FileTable *openPanel(MainWindow &window, unsigned index, const QString &path) {
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
    }
  private slots:
    void nativeMacTrashAcceptsTheWorkerThread() {
#ifndef Q_OS_MACOS
        QSKIP("The macOS native Trash integration is checked on its owning platform.");
#else
        QTemporaryDir fixture;
        const QString path = fixture.filePath("worker trash fixture.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("fixture bytes"), qint64(13));
        file.close();
        class NativeTrashReceipt final : public IQtTrashService {
          public:
            QString moved;
            bool MoveToTrash(const QString &source) override {
                return QFile::moveToTrash(source, &moved);
            }
        } trash;
        const auto cleanup = qScopeGuard([&] {
            if (!trash.moved.isEmpty())
                QFile::remove(trash.moved);
        });
        QTimer errors;
        dialogs_.connectResponder(errors, &errors, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog()))
                box->accept();
        });
        errors.start(1);
        UStringVector paths;
        paths.Add(ToUString(path));
        QCOMPARE(QtMoveItemsToTrash(paths, trash, nullptr), S_OK);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!trash.moved.isEmpty());
        QFile moved(trash.moved);
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("fixture bytes"));
        moved.close();
        QVERIFY(QFile::remove(trash.moved));
        trash.moved.clear();
#endif
    }
    void cancelledTrashStopsBeforeTheNextItem_data() {
        QTest::addColumn<bool>("lastItem");
        QTest::newRow("another-item-pending") << false;
        QTest::newRow("last-item-already-moving") << true;
    }
    void cancelledTrashStopsBeforeTheNextItem() {
        QFETCH(bool, lastItem);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        for (const QString &name : {QString("a.bin"), QString("b.bin")}) {
            QFile file(fixture.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("protected"), qint64(9));
        }
        ControlledBackend backend;
        Trash trash;
        trash.failAfter = -1;
        MainWindow window(&backend, {}, &trash);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 2);
        auto &panel = *view->sourcePanel;
        CRecordVector<UInt32> indices;
        indices.Add(0);
        if (!lastItem)
            indices.Add(1);
        const QString first = ToQString(panel.GetItemRelPath2(indices[0]));
        const QString second = ToQString(panel.GetItemRelPath2(1));
        const int focused = filesystemRow(window, first);
        QVERIFY(focused >= 0);
        view->setCurrentCell(focused, 0);
        view->selectRow(focused);
        if (!lastItem) {
            const int selected = filesystemRow(window, second);
            QVERIFY(selected >= 0);
            view->selectionModel()->select(view->model()->index(selected, 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        bool stopped = false;
        QSemaphore firstMoved, resume;
        trash.during = [&] {
            firstMoved.release();
            resume.acquire();
        };
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog()))
                ReplyDelete(*confirmation);
            else if (auto *question = qobject_cast<QMessageBox *>(dialogs_.activeDialog());
                     question && question->objectName() == "progressCancelQuestion")
                question->button(QMessageBox::Yes)->click();
            else if (auto *progress = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog());
                     progress && TestProgressDialog::IsCloseButton(
                                     progress->findChild<QPushButton *>("IDCANCEL"))) {
                progress->findChild<QPushButton *>("IDCANCEL")->click();
            } else if (auto *progress = dynamic_cast<CProgressDialog *>(dialogs_.activeDialog());
                       progress && firstMoved.tryAcquire()) {
                const auto release = qScopeGuard([&] { resume.release(); });
                QTRY_VERIFY(progress->findChild<QLabel *>("IDT_PROGRESS_FILE_NAME")
                                ->text()
                                .contains(first));
                QVERIFY(UiInspection::writeReport(*progress, "trash-progress-before-cancel"));
                auto *cancel = progress->findChild<QPushButton *>("IDCANCEL");
                QVERIFY(cancel);
                stopped = true;
                cancel->click();
            }
        });
        responder.start(1);
        CSelectedState selected;
        std::optional<QStringList> review;
        QCOMPARE(panel.MoveItemsToTrash(trash, selected, &indices, true, nullptr, &review),
                 lastItem ? S_OK : E_ABORT);
        responder.stop();
        if (lastItem) {
            QVERIFY(!review);
        } else {
            QVERIFY(review);
            QVERIFY(review->contains(QStringLiteral("Items moved to Trash (1)")));
        }
        QVERIFY(stopped);
        QCOMPARE(trash.calls, 1);
        QVERIFY(!QFileInfo::exists(fixture.filePath(first)));
        QFile remaining(fixture.filePath(second));
        QVERIFY(remaining.open(QIODevice::ReadOnly));
        QCOMPARE(remaining.readAll(), QByteArray("protected"));
        QTRY_COMPARE(view->rowCount(), 1);
        QCOMPARE(view->currentIndex().siblingAtColumn(0).data().toString(), second);
        if (!lastItem)
            QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QVERIFY(panel.ShouldProcessNotifications());
    }
    void folderAndLinkConsequences_data() {
        QTest::addColumn<bool>("link");
        QTest::addColumn<bool>("dangling");
        QTest::newRow("folder") << false << false;
        QTest::newRow("directory-link") << true << false;
        QTest::newRow("dangling-link") << true << true;
    }
    void folderAndLinkConsequences() {
        QFETCH(bool, link);
        QFETCH(bool, dangling);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        const QString directory = fixture.filePath("folder 空");
        const QString alias = fixture.filePath("link 空");
        QVERIFY(QDir().mkpath(directory));
        QFile payload(directory + "/protected.txt");
        QVERIFY(payload.open(QIODevice::WriteOnly));
        QCOMPARE(payload.write("protected bytes"), qint64(15));
        payload.close();
        if (link) {
            const QString target = dangling ? fixture.filePath("missing folder") : directory;
            QVERIFY(CreateNativeSymbolicLink(alias, target));
        }
        ControlledBackend backend;
        Trash trash;
        trash.failAfter = link ? -1 : 0;
        MainWindow window(&backend, {}, &trash);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        const QString name = QFileInfo(link ? alias : directory).fileName();
        const auto row = [&] {
            for (int i = 0; i < view->rowCount(); ++i)
                if (view->model()->index(i, 0).data().toString() == name)
                    return i;
            return -1;
        };
        QTRY_VERIFY(row() >= 0);
        view->setCurrentCell(row(), 0);
        view->selectRow(row());
        view->setFocus();
        bool seen = false;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *confirmation =
                    dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog())) {
                seen = true;
                QCOMPARE(confirmation->findChild<QLabel *>("deleteFolders")->isVisible(), !link);
                ReplyDelete(*confirmation, link);
            } else if (auto *dialog = dialogs_.activeDialog();
                       !TestProgressDialog::DismissVisibleResult(dialog)) {
                if (auto *box = qobject_cast<QMessageBox *>(dialog))
                    box->accept();
            }
        });
        responder.start(1);
        action(window, "deleteAction")->trigger();
        responder.stop();
        QVERIFY(seen);
        QCOMPARE(trash.calls, link ? 1 : 0);
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("protected bytes"));
        if (link) {
            QVERIFY(!QFileInfo(alias).isSymLink());
            QTRY_COMPARE(row(), -1);
        }
    }
    void flatTrashMovesSelectedFoldersAndTheirContentsOnce() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QTemporaryDir destination;
        QVERIFY(fixture.isValid() && destination.isValid());
        QVERIFY(QDir().mkpath(fixture.filePath("folder/nested")));
        QVERIFY(QDir().mkpath(fixture.filePath("folder-other")));
        const QString child = "folder/nested/child.txt";
        const QString separate = "folder-other/separate.txt";
        for (const QString &path : {child, separate}) {
            QFile file(fixture.filePath(path));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("content"), qint64(7));
        }
        class TrashToDirectory final : public IQtTrashService {
          public:
            QString destination;
            QStringList moved;
            bool MoveToTrash(const QString &path) override {
                const QString name = QFileInfo(path).fileName();
                if (!QDir().rename(path, QDir(destination).filePath(name)))
                    return false;
                moved.append(name);
                return true;
            }
        } trash;
        trash.destination = destination.path();
        ControlledBackend backend;
        MainWindow window(&backend, {}, &trash);
        window.show();
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_COMPARE(view->rowCount(), 2);
        auto &panel = *view->sourcePanel;
        panel.ChangeFlatMode();
        QTRY_COMPARE(view->rowCount(), 5);
        CRecordVector<UInt32> all, indices;
        panel.Get_ItemIndices_All(all);
        FOR_VECTOR(i, all) {
            const UInt32 index = all[i];
            const QString path = QDir::fromNativeSeparators(ToQString(panel.GetItemRelPath(index)));
            if (path == "folder" || path == "folder/nested" || path == child || path == separate)
                indices.Add(index);
        }
        QCOMPARE(indices.Size(), 4u);
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog()))
                ReplyDelete(*confirmation);
            else
                TestProgressDialog::DismissVisibleResult(dialogs_.activeDialog());
        });
        responder.start(1);
        CSelectedState selection;
        QCOMPARE(panel.MoveItemsToTrash(trash, selection, &indices), S_OK);
        responder.stop();
        QCOMPARE(trash.moved, (QStringList{"folder", "separate.txt"}));
        QVERIFY(QFileInfo::exists(destination.filePath(child)));
        QVERIFY(QFileInfo::exists(destination.filePath("separate.txt")));
        QTRY_COMPARE(view->rowCount(), 1);
    }
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
        QCOMPARE(panel.MoveItemsToTrash(trash, state, &indices, false), S_OK);
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
        QTest::addColumn<bool>("batch");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (bool cancel : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(cancel)))
                        << pane << language << cancel << false;
        QTest::newRow("batch-escape") << 0u << QString("-") << true << true;
    }
    void refusedConfirmation() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(bool, cancel);
        QFETCH(bool, batch);
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
        const QString otherName = QStringLiteral("other δ.bin");
        if (batch) {
            QFile other(fixture.filePath(otherName));
            QVERIFY(other.open(QIODevice::WriteOnly));
            QCOMPARE(other.write("other bytes"), qint64(11));
        }
        ControlledBackend backend;
        Trash trash;
        MainWindow window(&backend, {}, &trash);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, peerDirectory.path());
        QTRY_COMPARE(peer->rowCount(), 0);
        CMyComPtr<IFolderFolder> peerFolder = peer->sourcePanel->GetFolder();
        auto *view = openPanel(window, pane, fixture.path());
        const int itemCount = batch ? 2 : 1;
        QTRY_COMPARE(view->rowCount(), itemCount);
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        view->selectRow(0);
        if (batch)
            view->selectionModel()->select(view->model()->index(1, 0),
                                           QItemSelectionModel::Select | QItemSelectionModel::Rows);
        view->setCurrentCell(0, 0, QItemSelectionModel::NoUpdate);
        view->setFocus();
        CMyComPtr<IFolderFolder> folder = panel.GetFolder();
        const auto selection = view->selectionModel()->selection();
        bool observed = false;
        bool cancelDefault = false;
        bool reviewedByKeyboard = false;
        QString title, message;
        QStringList listedNames;
        bool reports = true;
        QTimer refusal;
        dialogs_.connectResponder(refusal, &window, [&] {
            auto *box = dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog());
            if (!box)
                return;
            refusal.stop();
            observed = true;
            cancelDefault = box->findChild<QPushButton *>("deleteCancel")->isDefault();
            title = box->windowTitle();
            message = box->findChild<QWidget *>("deleteSourceSummary")->accessibleName();
            if (auto *names = box->findChild<QListView *>("deleteSelectedNames"))
                for (int row = 0; row < names->model()->rowCount(); ++row)
                    listedNames.append(names->model()->index(row, 0).data().toString());
            for (const QSize size : {QSize(400, 200), QSize(300, 180)}) {
                box->resize(size);
                QApplication::processEvents();
                reports &= UiInspection::writeReport(*box, QString("source-delete-refused-%1-%2")
                                                               .arg(QTest::currentDataTag())
                                                               .arg(size.width()));
                for (auto *button : box->findChild<QDialogButtonBox *>("deleteButtons")->buttons())
                    reports &= UiInspection::fullyVisible(*button, *box);
            }
            QFile external(fixture.filePath("external.bin"));
            if (external.open(QIODevice::WriteOnly))
                external.write("external bytes");
            if (batch) {
                auto *names = box->findChild<QListView *>("deleteSelectedNames");
                for (int step = 0; names && !names->hasFocus() && step < 8; ++step)
                    QTest::keyClick(box->focusWidget(), Qt::Key_Tab);
                reviewedByKeyboard = names && names->hasFocus();
                if (reviewedByKeyboard) {
                    QTest::keyClick(names, Qt::Key_End);
                    QTest::keyClick(names, Qt::Key_Escape);
                    return;
                }
            }
            QTest::keyClick(box, cancel ? Qt::Key_Escape : Qt::Key_Return);
        });
        refusal.start(5);
#ifdef Q_OS_MACOS
        PanelKeys::command(view, Qt::Key_Backspace, Qt::ControlModifier);
#else
        QTest::keyClick(view, Qt::Key_Delete);
#endif
        refusal.stop();
        QVERIFY(observed);
        QVERIFY(cancelDefault);
        QCOMPARE(title, QCoreApplication::translate("DeleteRedesign", "Move to Trash?"));
        if (batch) {
            QVERIFY(reviewedByKeyboard);
            QCOMPARE(listedNames, (QStringList{name, otherName}));
        } else
            QVERIFY(message.contains(name));
        QVERIFY(reports);
        // Original Delete returns before RefreshListCtrl on No or Cancel.
        QCOMPARE(view->rowCount(), itemCount);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QCOMPARE(view->selectionModel()->selection(), selection);
        QTRY_COMPARE(window.focusWidget(), view);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(trash.calls, 0);
        QCOMPARE(peer->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peer->rowCount(), 0);
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
        QVERIFY(QFileInfo::exists(fixture.filePath("external.bin")));
        keep.close();
        QVERIFY(WindowActivation::waitForActive(window));
        QTRY_VERIFY(view->hasFocus());
        const auto clearClipboard = qScopeGuard([] { QApplication::clipboard()->clear(); });
        QTest::keySequence(view, QKeySequence::Copy);
        QTest::keyClick(view, Qt::Key_Tab);
        QTRY_VERIFY(peer->hasFocus());
        QTest::keySequence(peer, QKeySequence::Paste);
        QTRY_COMPARE(peer->rowCount(), itemCount);
        QFile copied(peerDirectory.filePath(name));
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("protected bytes"));
        if (batch) {
            QFile otherCopy(peerDirectory.filePath(otherName));
            QVERIFY(otherCopy.open(QIODevice::ReadOnly));
            QCOMPARE(otherCopy.readAll(), QByteArray("other bytes"));
            QVERIFY(QFileInfo::exists(fixture.filePath(otherName)));
        }
        QVERIFY(QFileInfo::exists(keep.fileName()));
        QCOMPARE(trash.calls, 0);
    }
    void accepted_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        QTest::addColumn<int>("mode");
        for (unsigned pane : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (int mode : {3, 4})
                    QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pane).arg(language).arg(mode)))
                        << pane << language << mode;
        QTest::newRow("partial-while-editing-address") << 0u << QString("-") << 5;
    }
    void accepted() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFETCH(int, mode);
        const bool partial = mode != 3;
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
        QtWorkspaceApplication application(*qApp);
        ControlledBackend observerBackend;
        MainWindow observer(&observerBackend);
        observer.show();
        observer.openPath(fixture.path());
        auto *observed = table(observer);
        QTRY_COMPARE(observed->rowCount(), 3);
        observed->sourcePanel->Set_AutoRefresh_Mode(false);
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
        bool refreshedBeforeReview = false;
        QString errorTitle;
        QString errorDetails;
        QTimer responder;
        dialogs_.connectResponder(responder, &window, [&] {
            if (auto *confirmation =
                    dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog())) {
                ++confirmations;
                QCOMPARE(confirmation->windowTitle(),
                         QCoreApplication::translate("DeleteRedesign", "Move to Trash?"));
                ReplyDelete(*confirmation);
                return;
            }
            if (auto *progress = TestProgressDialog::ResultWindow(dialogs_.activeDialog())) {
                if (progress->Sync.FinalMessage.Result == E_FAIL) {
                    ++errors;
                    errorTitle = progress->findChild<QLabel *>("IDT_PROGRESS_STATUS")->text();
                    errorDetails = TestProgressDialog::ReportText(*progress);
                    refreshedBeforeReview =
                        view->rowCount() == 2 && !itemNamed(firstDeleted).isValid();
                }
                for (const QSize size : {QSize(400, 240), QSize(340, 220)}) {
                    progress->resize(size);
                    QApplication::processEvents();
                    reports &=
                        UiInspection::writeReport(*progress, QString("source-delete-error-%1-%2")
                                                                 .arg(QTest::currentDataTag())
                                                                 .arg(size.width()));
                    reports &= UiInspection::fullyVisible(
                        *progress->findChild<QPushButton *>("IDCANCEL"), *progress);
                }
                TestProgressDialog::CloseResult(*progress);
                return;
            }
            auto *box = qobject_cast<QMessageBox *>(dialogs_.activeDialog());
            if (!box)
                return;
            ++errors;
            errorTitle = box->windowTitle();
            errorDetails = box->text();
#ifdef Q_OS_MACOS
            if (auto *progress = dynamic_cast<CProgressDialog *>(box->parentWidget()))
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
        });
        responder.start(1);
        action(window, "deleteAction")->trigger();
        if (mode == 5) {
            QApplication::setActiveWindow(&window);
            auto *sourcePane = window.findChild<QWidget *>("filePanel0");
            auto *address = sourcePane->findChild<QLineEdit *>("archivePath");
            PanelKeys::focusAddress(address);
            QTRY_VERIFY(address->hasFocus());
            address->selectAll();
            QTest::keyClicks(address, "unfinished path");
            QTRY_COMPARE(view->rowCount(), 2);
            QTest::qWait(100);
            QCOMPARE(errors, 0);
            QVERIFY(address->hasFocus());
            QTest::keyClicks(address, " draft");
            QCOMPARE(address->text(), QString("unfinished path draft"));
            QTest::keyClick(address, Qt::Key_Escape);
            QTRY_COMPARE(errors, 1);
        } else if (partial) {
            auto *menu = window.findChild<QMenu *>("viewMenu");
            QVERIFY(menu);
            menu->popup(window.mapToGlobal(QPoint(20, 20)));
            const auto closeMenu = qScopeGuard([&] { menu->hide(); });
            QTRY_VERIFY(menu->isVisible());
            QTRY_COMPARE(view->rowCount(), 2);
            QTest::qWait(100);
            QCOMPARE(errors, 0);
            QVERIFY(menu->isVisible());
            QTest::keyClick(menu, Qt::Key_Escape);
            QTRY_COMPARE(errors, 1);
        }
        responder.stop();
        QCOMPARE(confirmations, 1);
        QCOMPARE(errors, partial ? 1 : 0);
        QVERIFY(reports);
        if (errors)
            QCOMPARE(errorTitle,
                     QCoreApplication::translate("ProgressRedesign", "Operation failed"));
        QTRY_COMPARE(view->rowCount(), partial ? 2 : 1);
        QTRY_COMPARE(observed->rowCount(), partial ? 2 : 1);
        QCOMPARE(panel.GetFolder(), (IFolderFolder *)folder);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(trash.calls, 2);
        QVERIFY(!QFileInfo::exists(fixture.filePath(firstDeleted)));
        QCOMPARE(QFileInfo::exists(fixture.filePath(secondDeleted)), partial);
        QCOMPARE(view->currentIndex().data(QtPanelList::NameRole).toString(),
                 partial ? secondDeleted : QString("keep.bin"));
        if (partial) {
            QVERIFY(refreshedBeforeReview);
            QVERIFY(errorDetails.contains(secondDeleted));
            QVERIFY(errorDetails.contains(language == "ru"
                                              ? QStringLiteral("Перемещено в Корзину (1)")
                                              : QStringLiteral("Items moved to Trash (1)")));
            QVERIFY(errorDetails.contains(
                language == "ru" ? QStringLiteral("Не удалось переместить объект в Корзину.")
                                 : QStringLiteral("Could not move the item to Trash.")));
            QFile remaining(fixture.filePath(secondDeleted));
            QVERIFY(remaining.open(QIODevice::ReadOnly));
            QCOMPARE(remaining.readAll(), QByteArray("protected bytes"));
            remaining.close();
            QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
            QCOMPARE(view->selectionModel()->selectedRows().front().data(QtPanelList::NameRole),
                     QVariant(secondDeleted));
            trash.failAfter = -1;
            QVERIFY(WindowActivation::waitForActive(window));
            QTRY_COMPARE(window.focusWidget(), view);
            responder.start(1);
#ifdef Q_OS_MACOS
            PanelKeys::command(window.focusWidget(), Qt::Key_Backspace, Qt::ControlModifier);
#else
            QTest::keyClick(window.focusWidget(), Qt::Key_Delete);
#endif
            responder.stop();
            QCOMPARE(confirmations, 2);
            QCOMPARE(errors, 1);
            QCOMPARE(trash.calls, 3);
            QTRY_COMPARE(view->rowCount(), 1);
            QTRY_COMPARE(observed->rowCount(), 1);
            QVERIFY(!QFileInfo::exists(fixture.filePath(secondDeleted)));
            QCOMPARE(view->currentIndex().data(QtPanelList::NameRole).toString(),
                     QString("keep.bin"));
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
        QTest::addColumn<bool>("icons");
        QTest::newRow("table") << false;
        QTest::newRow("icons") << true;
    }
    void deferredRefresh() {
        QFETCH(bool, icons);
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
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, fixture.path());
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 258);
        peer->sourcePanel->Set_AutoRefresh_Mode(false);
        auto *view = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 258);
        QVERIFY(WindowActivation::waitForActive(window));
        auto &panel = *view->sourcePanel;
        panel.Set_AutoRefresh_Mode(false);
        const auto nameAt = [view](int row) {
            return view->model()->index(row, 0).data(QtPanelList::NameRole).toString();
        };
        const QString earlier = nameAt(120);
        const QString removed = nameAt(130);
        const QString next = nameAt(131);
        const QString first = nameAt(0);
        view->setCurrentCell(130, 0);
        view->selectRow(130);
        view->selectionModel()->select(view->model()->index(120, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        view->setFocus();
        QTRY_VERIFY(view->hasFocus());
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *active =
            icons ? view->parentWidget()->findChild<QAbstractItemView *>("iconList") : view;
        QVERIFY(active && active->isVisible());
        auto *address =
            window.findChild<QWidget *>("filePanel0")->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        QTRY_VERIFY(address->hasFocus());
        QTest::keyClicks(address, "unsubmitted path");
        active->setFocus();
        QSemaphore resume;
        bool sawProgress = false;
        trash.during = [&] {
            if (trash.calls == 1)
                resume.tryAcquire(1, 5000);
        };
        QTimer answer;
        dialogs_.connectResponder(answer, &window, [&] {
            if (auto *box = dynamic_cast<QtDeleteConfirmation *>(dialogs_.activeDialog())) {
                auto *confirm = box->findChild<QPushButton *>("deleteConfirm");
                confirm->setFocus();
                QTest::keyClick(confirm, Qt::Key_Return);
            } else if (dynamic_cast<CProgressDialog *>(dialogs_.activeDialog())) {
                answer.stop();
                sawProgress = true;
                resume.release();
            }
        });
        answer.start(5);
        action(window, "deleteAction")->trigger();
        answer.stop();
        QVERIFY(sawProgress);
        QTRY_VERIFY(view->isEnabled() && view->rowCount() == 256);
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 256);
        QCOMPARE(window.focusWidget(), active);
        QCOMPARE(address->text(), QString("unsubmitted path"));
        QVERIFY(panel.ShouldProcessNotifications());
        QVERIFY(!QFileInfo::exists(fixture.filePath(removed)));
        QVERIFY(!QFileInfo::exists(fixture.filePath(earlier)));
        QCOMPARE(nameAt(view->currentRow()), next);
        QTRY_VERIFY(
            active->visualRect(active->currentIndex()).intersects(active->viewport()->rect()));
        QApplication::clipboard()->clear();
        const auto clearClipboard = qScopeGuard([] { QApplication::clipboard()->clear(); });
        QTest::keyClick(window.focusWidget(), Qt::Key_Home);
        QCOMPARE(nameAt(view->currentRow()), first);
        QTest::keySequence(window.focusWidget(), QKeySequence::Copy);
        QTRY_VERIFY(QApplication::clipboard()->mimeData() &&
                    QApplication::clipboard()->mimeData()->hasUrls());
        QCOMPARE(QApplication::clipboard()->mimeData()->urls(),
                 QList{QUrl::fromLocalFile(fixture.filePath(first))});
        QFile keep(fixture.filePath("entry-257.bin"));
        QVERIFY(keep.open(QIODevice::ReadOnly));
        QCOMPARE(keep.readAll(), QByteArray("protected bytes"));
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(SourceDeleteTests)
#include "source_delete_tests.moc"
