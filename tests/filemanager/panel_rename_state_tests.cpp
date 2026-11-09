#include "filemanager/panels/Panel.h"
#include "filemanager/providers/FSFolder.h"
#include "filemanager/support/selection_fixture.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "filemanager/window/qtworkspaceapplication.h"
#include "localization/LangUtils.h"
#include "platform/support/native_link_fixture.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "ui/support/layout_inspection.h"
#include <QEventLoop>
#include <QFile>
#include <QFocusFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QScopeGuard>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class PanelRenameStateTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
  private slots:
    void relativeDestinationSelectsVisibleItem_data() {
        QTest::addColumn<QString>("destination");
        QTest::addColumn<QString>("visible");
        QTest::addColumn<bool>("throughLink");
        QTest::addColumn<bool>("unchanged");
        QTest::addColumn<bool>("flat");
        QTest::newRow("same-folder")
            << QString("./renamed δ.txt") << QString("renamed δ.txt") << false << false << false;
        QTest::newRow("case-only")
            << QString("Original.txt") << QString("Original.txt") << false << false << false;
        QTest::newRow("subfolder") << QString("target folder/renamed δ.txt")
                                   << QString("target folder") << false << false << false;
        QTest::newRow("flat-subfolder") << QString("target folder/./renamed δ.txt")
                                        << QString("renamed δ.txt") << false << false << true;
        QTest::newRow("same-name-in-subfolder")
            << QString("target folder/original.txt") << QString("target folder") << false << false
            << false;
        QTest::newRow("unchanged-relative-path")
            << QString("target folder/../original.txt") << QString("original.txt") << false << true
            << false;
#ifdef Q_OS_UNIX
        QTest::newRow("linked-folder")
            << QString("./renamed δ.txt") << QString("renamed δ.txt") << true << false << false;
#endif
    }
    void relativeDestinationSelectsVisibleItem() {
        QFETCH(QString, destination);
        QFETCH(QString, visible);
        QFETCH(bool, throughLink);
        QFETCH(bool, unchanged);
        QFETCH(bool, flat);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QTemporaryDir aliases;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("original.txt");
        QVERIFY(write(source, "original bytes"));
        QVERIFY(write(fixture.filePath("keep.txt"), "keep bytes"));
        QVERIFY(QDir().mkdir(fixture.filePath("target folder")));
        QtWorkspaceApplication application(*qApp);
        ControlledBackend backend, observerBackend;
        MainWindow window(&backend), observer(&observerBackend);
        const QString output = fixture.filePath(destination);
        observer.show();
        observer.openPath(QFileInfo(output).absolutePath());
        QTRY_VERIFY(table(observer)->isEnabled());
        table(observer)->sourcePanel->Set_AutoRefresh_Mode(false);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *view = table(window);
        if (throughLink) {
            QVERIFY(aliases.isValid());
            const QString alias = aliases.filePath("folder link");
            QVERIFY(QFile::link(fixture.path(), alias));
            COpenResult opened;
            QCOMPARE(view->sourcePanel->BindToPath(ToUString(alias + '/'), UString(), opened),
                     S_OK);
            QCOMPARE(view->sourcePanel->RefreshListCtrl(), S_OK);
            QCOMPARE(QDir::cleanPath(ToQString(view->sourcePanel->GetCurrentFolderPrefix())),
                     alias);
        }
        QTRY_COMPARE(view->rowCount(), 3);
        if (flat) {
            view->sourcePanel->ChangeFlatMode();
            QTRY_VERIFY(view->sourcePanel->GetFlatMode());
        }
        const int row = filesystemRow(window, "original.txt");
        QVERIFY(row >= 0);
        view->setCurrentCell(row, 0);
        view->selectRow(row);
        view->setFocus();
        action(window, "renameAction")->trigger();
        auto *editor = view->findChild<QLineEdit *>();
        QVERIFY(editor && editor->isVisible());
        bool errorShown = false;
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &window, [&] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (TestProgressDialog::DismissVisibleResult(widget))
                    errorShown = true;
        });
        closeError.start(2);
        editor->setText(destination);
        QTest::keyClick(editor, Qt::Key_Return);
        QCoreApplication::processEvents();
        QTRY_VERIFY(QFileInfo::exists(output));
        QTRY_VERIFY(filesystemRow(observer, QFileInfo(output).fileName()) >= 0);
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Files).contains("original.txt"), unchanged);
        QVERIFY(QDir(QFileInfo(output).absolutePath())
                    .entryList(QDir::Files)
                    .contains(QFileInfo(output).fileName()));
        QVERIFY(!errorShown);
        QTRY_COMPARE(view->currentIndex().data().toString(), visible);
        if (flat)
            QCOMPARE(QDir::cleanPath(
                         view->currentIndex().data(QtPanelList::OperationPathRole).toString()),
                     QDir::cleanPath(output));
        QCOMPARE(filesystemRow(window, "original.txt") >= 0, unchanged);
        QVERIFY(view->selectionModel()->isRowSelected(view->currentRow(), {}));
        QFile payload(output);
        QVERIFY(payload.open(QIODevice::ReadOnly));
        QCOMPARE(payload.readAll(), QByteArray("original bytes"));
    }

    void activationUsesFocusedRow_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<bool>("icons");
        QTest::addColumn<bool>("link");
        for (unsigned pane : {0u, 1u})
            for (bool icons : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(pane).arg(icons)))
                    << pane << icons << false;
        QTest::newRow("link-table") << 0u << false << true;
        QTest::newRow("link-icons") << 1u << true << true;
    }
    void activationUsesFocusedRow() {
        QFETCH(unsigned, pane);
        QFETCH(bool, icons);
        QFETCH(bool, link);
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("focused 文件.txt");
        const QString peer = QStringLiteral("marked δ.txt");
        const QString renamed = QStringLiteral("renamed 空.txt");
        QTemporaryDir linkTarget;
        QVERIFY(linkTarget.isValid());
        const QString target = linkTarget.filePath("keep-target.bin");
        if (link) {
            QVERIFY(write(target, "focus bytes"));
            QVERIFY(CreateNativeSymbolicLink(fixture.filePath(original), target));
        } else
            QVERIFY(write(fixture.filePath(original), "focus bytes"));
        QVERIFY(write(fixture.filePath(peer), "mark bytes"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 2);
        view->sourcePanel->Set_AutoRefresh_Mode(false);
        int focusedRow = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 3).toString() == original)
                focusedRow = row;
        QVERIFY(focusedRow >= 0);
        view->sourcePanel->SelectAll(true);
        view->selectionModel()->setCurrentIndex(view->model()->index(focusedRow, 0),
                                                QItemSelectionModel::NoUpdate);
        action(window, icons ? "viewMode0Action" : "viewMode3Action")->trigger();
        auto *active =
            icons ? static_cast<QAbstractItemView *>(ui->findChild<QListView *>("iconList"))
                  : static_cast<QAbstractItemView *>(view);
        QVERIFY(active);
        active->setFocus();
        UStringVector savedMarks;
        view->sourcePanel->GetSelectedNames(savedMarks);
        QVERIFY(action(window, "renameAction")->isEnabled());
        QTest::keyClick(active, Qt::Key_F2);
        QPointer<QLineEdit> editor = active->findChild<QLineEdit *>();
        QVERIFY(editor);
        QCOMPARE(editor->text(), original);
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(editor);
            QVERIFY(UiInspection::fullyVisible(*editor, window));
            if (!icons)
                QVERIFY(editor->height() >= editor->minimumSizeHint().height());
            for (auto *frame : active->findChildren<QFocusFrame *>())
                if (frame->isVisible() && frame->widget() == editor) {
                    const QRect ring = UiInspection::bounds(*frame, *active->viewport());
                    QVERIFY2(active->viewport()->rect().contains(ring),
                             qPrintable(QString("focus frame %1,%2 %3x%4 exceeds parent %5x%6")
                                            .arg(frame->x())
                                            .arg(frame->y())
                                            .arg(frame->width())
                                            .arg(frame->height())
                                            .arg(frame->parentWidget()->width())
                                            .arg(frame->parentWidget()->height())));
                    if (!icons) {
                        const QRect row = view->visualRect(view->model()->index(focusedRow, 0));
                        QVERIFY(ring.top() >= row.top());
                        QVERIFY(ring.bottom() <= row.bottom());
                    }
                }
            QVERIFY(UiInspection::writeReport(
                window,
                QString("rename-activation-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(!active->findChild<QLineEdit *>());
        QVERIFY(QFileInfo::exists(fixture.filePath(original)));
        UStringVector remainingMarks;
        view->sourcePanel->GetSelectedNames(remainingMarks);
        QCOMPARE(remainingMarks.Size(), savedMarks.Size());
        FOR_VECTOR(i, savedMarks)
        QCOMPARE(remainingMarks[i], savedMarks[i]);
        if (!icons) {
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum());
            const int beforeEdit = view->horizontalScrollBar()->value();
            QVERIFY(beforeEdit > 0);
            QTest::keyClick(active, Qt::Key_F2);
            editor = active->findChild<QLineEdit *>();
            QVERIFY(editor);
            QVERIFY(view->horizontalScrollBar()->value() < beforeEdit);
            QTest::keyClick(editor, Qt::Key_Escape);
            QTRY_COMPARE(view->horizontalScrollBar()->value(), beforeEdit);
        }
        action(window, "renameAction")->trigger();
        editor = active->findChild<QLineEdit *>();
        QVERIFY(editor);
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(QFileInfo::exists(fixture.filePath(renamed)));
        QVERIFY(!QFileInfo::exists(fixture.filePath(original)));
        QFile preserved(fixture.filePath(peer));
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("mark bytes"));
        QFile changed(fixture.filePath(renamed));
        QVERIFY(changed.open(QIODevice::ReadOnly));
        QCOMPARE(changed.readAll(), QByteArray("focus bytes"));
        if (link) {
            QVERIFY(QFileInfo(fixture.filePath(renamed)).isSymLink());
            QCOMPARE(QFileInfo(fixture.filePath(renamed)).symLinkTarget(), target);
            QFile untouched(target);
            QVERIFY(untouched.open(QIODevice::ReadOnly));
            QCOMPARE(untouched.readAll(), QByteArray("focus bytes"));
        }
    }
    void tabFinishesOneRename_data() {
        QTest::addColumn<bool>("reverse");
        QTest::newRow("tab") << false;
        QTest::newRow("shift-tab") << true;
    }

    void tabFinishesOneRename() {
        QFETCH(bool, reverse);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        QVERIFY(write(fixture.filePath("a.txt"), "first"));
        QVERIFY(write(fixture.filePath("b.txt"), "rename me"));
        QVERIFY(write(fixture.filePath("c.txt"), "last"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        window.openPath(fixture.path());
        auto *view = table(window);
        QTRY_VERIFY(view->isEnabled() && filesystemRow(window, "b.txt") >= 0);
        view->setFocus();
        view->setCurrentCell(filesystemRow(window, "b.txt"), 0);
        view->selectRow(view->currentRow());
        QTest::keyClick(view, Qt::Key_F2);
        auto *editor = qobject_cast<QLineEdit *>(window.focusWidget());
        QVERIFY(editor && view->isAncestorOf(editor));
        editor->setText("renamed.txt");
        QTest::keyClick(editor, reverse ? Qt::Key_Backtab : Qt::Key_Tab,
                        reverse ? Qt::ShiftModifier : Qt::NoModifier);
        QCOMPARE(window.focusWidget(), view);
        QTRY_VERIFY(filesystemRow(window, "renamed.txt") >= 0);
        QCOMPARE(view->currentIndex().data().toString(), QString("renamed.txt"));
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QTest::keyClick(view, Qt::Key_F2);
        editor = qobject_cast<QLineEdit *>(window.focusWidget());
        QVERIFY(editor && view->isAncestorOf(editor));
        QCOMPARE(editor->text(), QString("renamed.txt"));
        editor->setText("finished.txt");
        QTest::keyClick(editor, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, "finished.txt") >= 0);
        QVERIFY(!QFileInfo::exists(fixture.filePath("b.txt")));
        QVERIFY(!QFileInfo::exists(fixture.filePath("renamed.txt")));
        QFile finished(fixture.filePath("finished.txt"));
        QVERIFY(finished.open(QIODevice::ReadOnly));
        QCOMPARE(finished.readAll(), QByteArray("rename me"));
    }

    void editorPreservesSavedSelection_data() {
        QTest::addColumn<unsigned>("sourceIndex");
        QTest::addColumn<QString>("language");
        QTest::addColumn<bool>("collision");
        QTest::addColumn<QString>("invalidName");
        QTest::addColumn<bool>("markFocused");
        QTest::addColumn<bool>("icons");
        for (unsigned index : {0u, 1u})
            for (const QString language : {"-", "ru"})
                for (int outcome : {0, 1, 2, 3, 4})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3").arg(index).arg(language).arg(outcome)))
                        << index << language << (outcome == 1)
                        << (outcome == 2   ? QString("..")
                            : outcome == 3 ? QStringLiteral("renamed 空.txt") + QChar::Null + "tail"
                            : outcome == 4 ? QStringLiteral("")
                                           : QString())
                        << true << false;
        QTest::newRow("unmarked-focused-row-collision")
            << 0u << QString("-") << true << QString() << false << false;
        QTest::newRow("icons-collision") << 1u << QString("-") << true << QString() << true << true;
#ifdef Q_OS_WIN
        QTest::newRow("empty-path-component")
            << 0u << QString("-") << false << QStringLiteral("./.../renamed 空.txt") << true
            << false;
#endif
    }
    void editorPreservesSavedSelection() {
        QFETCH(unsigned, sourceIndex);
        QFETCH(QString, language);
        QFETCH(bool, collision);
        QFETCH(QString, invalidName);
        QFETCH(bool, markFocused);
        QFETCH(bool, icons);
        const bool invalid = !invalidName.isNull();
        const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
        QFile::remove(settings);
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        CFmSettings preferences;
        preferences.Load(settings);
        preferences.ShowDots = false;
        preferences.Save(settings);
        const auto reset = qScopeGuard([&] {
            SaveRegLang(L"-", settings);
            ReloadLang(settings);
        });
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("original 文件.txt");
        const QString peer = QStringLiteral("peer δ.txt");
        const QString renamed = invalid     ? invalidName
                                : collision ? peer
                                            : QStringLiteral("renamed 空.txt");
        QVERIFY(write(fixture.filePath(original), "original bytes"));
        QVERIFY(write(fixture.filePath(peer), "peer bytes"));
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        action(window, "twoPanelsAction")->trigger();
        auto *ui = window.findChild<QWidget *>(QString("filePanel%1").arg(sourceIndex));
        auto *view = static_cast<FileTable *>(ui->findChild<FileTable *>("archiveTable"));
        auto *address = ui->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(fixture.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(view->rowCount(), 2);
        for (const auto &table : window.findChildren<FileTable *>("archiveTable"))
            static_cast<FileTable *>(table)->sourcePanel->Set_AutoRefresh_Mode(false);
        const auto findRow = [&](const QString &name) {
            for (int row = 0; row < view->rowCount(); ++row)
                if (view->model()->index(row, 0).data(Qt::UserRole + 3).toString() == name)
                    return row;
            return -1;
        };
        const int sourceRow = findRow(original);
        const int peerRow = findRow(peer);
        QVERIFY(sourceRow >= 0 && peerRow >= 0);
        auto *peerUi = window.findChild<QWidget *>(QString("filePanel%1").arg(1 - sourceIndex));
        auto *peerView = static_cast<FileTable *>(peerUi->findChild<FileTable *>("archiveTable"));
        CMyComPtr<IFolderFolder> peerFolder = peerView->sourcePanel->GetFolder();
        const UString peerPrefix = peerView->sourcePanel->GetCurrentFolderPrefix();
        const int peerRows = peerView->rowCount();
        view->selectRow(sourceRow);
        view->setFocus();
        if (icons)
            action(window, "viewMode0Action")->trigger();
        auto *active = icons ? ui->findChild<QAbstractItemView *>("iconList") : view;
        QTRY_COMPARE(window.focusWidget(), active);
        const int sizeColumn = view->columnForProperty(kpidSize);
        if (!icons) {
            QVERIFY(sizeColumn > 0 && !view->isColumnHidden(sizeColumn));
            view->setCurrentCell(sourceRow, sizeColumn, QItemSelectionModel::NoUpdate);
            view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->maximum());
        }
        const int horizontalPosition = view->horizontalScrollBar()->value();
        QVERIFY(action(window, "renameAction")->isEnabled());
        action(window, "renameAction")->trigger();
        auto *editor = active->findChild<QLineEdit *>();
        QVERIFY(editor);
        // A selection change during the editor must survive its source snapshot.
        if (!markFocused)
            view->clearSelection();
        view->selectionModel()->select(view->model()->index(peerRow, 0),
                                       QItemSelectionModel::Select | QItemSelectionModel::Rows);
        const int expectedMarks = markFocused ? 2 : 1;
        QCOMPARE(view->selectionModel()->selectedRows().size(), expectedMarks);
        bool errorShown = false;
        QString errorText;
        QMessageBox::Icon errorIcon = QMessageBox::NoIcon;
        bool errorGeometry = true;
        QEventLoop errorDelivery;
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &window, [&] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (collision && TestProgressDialog::DismissVisibleResult(widget)) {
                    errorShown = true;
                    errorDelivery.quit();
                    continue;
                }
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    errorShown = true;
                    errorText = box->text();
                    errorIcon = box->icon();
                    if (invalid && !box->testAttribute(Qt::WA_DontShowOnScreen)) {
                        for (const QSize size : {QSize(440, 220), QSize(340, 220)}) {
                            box->resize(size);
                            QApplication::processEvents();
                            auto *label = box->findChild<QLabel *>("qt_msgbox_label");
                            auto *ok = box->button(QMessageBox::Ok);
                            errorGeometry &= label && ok &&
                                             UiInspection::fullyVisible(*label, *box) &&
                                             UiInspection::fullyVisible(*ok, *box);
                            errorGeometry &=
                                UiInspection::writeReport(*box, QString("rename-invalid-%1-%2")
                                                                    .arg(QTest::currentDataTag())
                                                                    .arg(size.width()));
                        }
                    }
                    box->accept();
                    errorDelivery.quit();
                }
            }
        });
        closeError.start(2);
        CMyComPtr<IFolderFolder> retained = view->sourcePanel->GetFolder();
        auto *filesystem = dynamic_cast<NFsFolder::CFSFolder *>((IFolderFolder *)retained);
        QVERIFY(filesystem);
        unsigned loads = 0;
        const auto resetCheckBreak = qScopeGuard([&] { filesystem->CheckBreak = {}; });
        filesystem->CheckBreak = [&](const QString &) {
            ++loads;
            return true;
        };
        editor->setText(renamed);
        QTest::keyClick(editor, Qt::Key_Return);
        if (collision || invalid) {
            // Cocoa native modal callbacks need a real Qt event loop, rather
            // than QTRY's manual processEvents pump. Keep the same 5s limit.
            QTimer::singleShot(5000, &errorDelivery, &QEventLoop::quit);
            if (!errorShown)
                errorDelivery.exec();
            QVERIFY(errorShown);
        }
        if (invalid) {
            QCOMPARE(errorIcon, QMessageBox::Critical);
            QCOMPARE(errorText, ToQString(HResultToMessage(E_INVALIDARG)));
            QVERIFY(errorGeometry);
        }
        QTRY_COMPARE(view->model()->index(view->currentRow(), 0).data(Qt::UserRole + 3).toString(),
                     collision || invalid ? original : renamed);
        if (!icons) {
            QCOMPARE(view->currentIndex().column(), sizeColumn);
            QCOMPARE(view->horizontalScrollBar()->value(), horizontalPosition);
        }
        QCOMPARE(view->selectionModel()->selectedRows().size(), expectedMarks);
        if (!markFocused)
            QCOMPARE(view->selectionModel()->selectedRows().front().data().toString(), peer);
        closeError.stop();
        filesystem->CheckBreak = {};
        QCOMPARE(loads == 0, invalid);
        QVERIFY(view->sourcePanel->ShouldDisplayItemText());
        QVERIFY(view->sourcePanel->ShouldProcessNotifications());
        QCOMPARE(view->sourcePanel->GetFolder(), (IFolderFolder *)retained);
        QCOMPARE(peerView->sourcePanel->GetFolder(), (IFolderFolder *)peerFolder);
        QCOMPARE(peerView->sourcePanel->GetCurrentFolderPrefix(), peerPrefix);
        QCOMPARE(peerView->rowCount(), peerRows);
        CRecordVector<UInt32> selected;
        view->sourcePanel->Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), unsigned(expectedMarks));
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            QVERIFY(UiInspection::fullyVisible(*active, window));
            QVERIFY(UiInspection::writeReport(
                window,
                QString("rename-selection-%1-%2").arg(QTest::currentDataTag()).arg(size.width())));
        }
        for (const auto &entry :
             {qMakePair(collision || invalid ? original : renamed, QByteArray("original bytes")),
              qMakePair(peer, QByteArray("peer bytes"))}) {
            QFile payload(fixture.filePath(entry.first));
            QVERIFY(payload.open(QIODevice::ReadOnly));
            QCOMPARE(payload.readAll(), entry.second);
        }
        if (collision || (invalid && invalidName != "..")) {
            QVERIFY(!QFileInfo::exists(fixture.filePath(QStringLiteral("renamed 空.txt"))));
            QTRY_COMPARE(window.focusWidget(), active);
            QTest::keyClick(window.focusWidget(), Qt::Key_F2);
            QLineEdit *retry = nullptr;
            for (auto *candidate : active->findChildren<QLineEdit *>())
                if (candidate->isVisible())
                    retry = candidate;
            QVERIFY(retry);
            const QString accepted = QStringLiteral("retry 空.txt");
            retry->setText(accepted);
            QTest::keyClick(retry, Qt::Key_Return);
            QTRY_VERIFY(QFileInfo::exists(fixture.filePath(accepted)));
            QVERIFY(!QFileInfo::exists(fixture.filePath(original)));
            QFile payload(fixture.filePath(accepted));
            QVERIFY(payload.open(QIODevice::ReadOnly));
            QCOMPARE(payload.readAll(), QByteArray("original bytes"));
        }
    }
    void sourceStateAndDeferredText_data() {
        QTest::addColumn<bool>("collision");
        QTest::addColumn<bool>("cancelLoad");
        for (bool collision : {false, true})
            for (bool cancelLoad : {false, true})
                QTest::newRow(qPrintable(QString("%1-%2").arg(collision).arg(cancelLoad)))
                    << collision << cancelLoad;
    }
    void sourceStateAndDeferredText() {
        QFETCH(bool, collision);
        QFETCH(bool, cancelLoad);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString original = QStringLiteral("original 文件.txt");
        const QString peer = QStringLiteral("peer δ.txt");
        const QString renamed = collision ? peer : QStringLiteral("renamed 空.txt");
        QVERIFY(write(fixture.filePath(original), "original bytes"));
        QVERIFY(write(fixture.filePath(peer), "peer bytes"));
        auto *filesystem = new NFsFolder::CFSFolder(fixture.path());
        CMyComPtr<IFolderFolder> folder = filesystem;
        CPanel panel(folder);
        FileTable view(2);
        view.sourcePanel = &panel;
        view.textFolder = folder;
        view.setColumnProperties({kpidName, kpidSize});
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        panel.AttachSelectionView(&view);
        QCOMPARE(panel.LoadListItems(), S_OK);
        const auto publish = [&] {
            UInt32 count = 0;
            folder->GetNumberOfItems(&count);
            view.clearRows();
            for (UInt32 i = 0; i < count; ++i) {
                FilePanelModel::Row item;
                item.sourceIndex = i;
                item.name = ToQString(panel.GetItemName(i));
                view.panelModel()->append(std::move(item));
            }
        };
        publish();
        QCOMPARE(panel.RefreshListCtrl(), S_OK);
        int row = -1;
        for (int i = 0; i < view.rowCount(); ++i)
            if (panel.GetItemName(UInt32(i)) == ToUString(original))
                row = i;
        QVERIFY(row >= 0);
        view.setCurrentCell(row, 0);
        panel.SelectAll(true);
        const QString cachedName = view.model()->index(row, 0).data().toString();
        QCOMPARE(cachedName, original);
        QTimer closeError;
        connect(&closeError, &QTimer::timeout, &view, [] {
            for (QWidget *widget : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    box->accept();
                else
                    TestProgressDialog::DismissVisibleResult(widget);
        });
        closeError.start(2);
        const HRESULT result = panel.OnEndLabelEdit(UInt32(row), ToUString(renamed));
        closeError.stop();
        QCOMPARE(result == S_OK, !collision);
        const bool blank = view.model()->index(row, 0).data().toString().isEmpty();
        const auto state = panel.TakeRefreshSelection();
        QVERIFY2(blank && state.has_value(),
                 qPrintable(QString("blank=%1 pending=%2").arg(blank).arg(state.has_value())));
        QCOMPARE(view.model()->index(row, 0).data(Qt::EditRole).toString(), original);
        QCOMPARE(view.model()->index(row, 0).data(Qt::UserRole + 6).toUInt(), UInt32(row));
        QVERIFY(state->FocusedName_Defined);
        QVERIFY(state->SelectFocused);
        QCOMPARE(state->FocusedName, ToUString(collision ? original : renamed));
        QCOMPARE(state->SelectedNames.Size(), 2u);
        QVERIFY(state->SelectedNames.FindInSorted(ToUString(original)) >= 0);
        QVERIFY(state->SelectedNames.FindInSorted(ToUString(peer)) >= 0);
        QVERIFY(!panel.TakeRefreshSelection().has_value());
        if (cancelLoad) {
            filesystem->CheckBreak = [](const QString &) { return false; };
            QCOMPARE(panel.LoadListItems(), E_ABORT);
            QCOMPARE(panel.RefreshListCtrl(*state), E_ABORT);
            QCOMPARE(view.rowCount(), 0);
            filesystem->CheckBreak = {};
        }
        QCOMPARE(panel.LoadListItems(), S_OK);
        publish();
        QCOMPARE(panel.RefreshListCtrl(*state), S_OK);
        const QString focused = collision ? original : renamed;
        QCOMPARE(view.model()->index(view.currentRow(), 0).data().toString(), focused);
        QCOMPARE(view.selectionModel()->selectedRows().size(), 2);
        CRecordVector<UInt32> selected;
        panel.Get_ItemIndices_Selected(selected);
        QCOMPARE(selected.Size(), 2u);
        QVERIFY(panel.ShouldProcessNotifications());
        QCOMPARE(QFileInfo::exists(fixture.filePath(original)), collision);
        for (const auto &entry : {qMakePair(focused, QByteArray("original bytes")),
                                  qMakePair(peer, QByteArray("peer bytes"))}) {
            QFile payload(fixture.filePath(entry.first));
            QVERIFY(payload.open(QIODevice::ReadOnly));
            QCOMPARE(payload.readAll(), entry.second);
        }
    }
};
#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(PanelRenameStateTests)
#include "panel_rename_state_tests.moc"
