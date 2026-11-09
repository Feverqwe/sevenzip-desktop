#include "7zip/PropID.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "ui/support/layout_inspection.h"

class ViewMenuTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static int row(FileTable &view, const QString &name) {
        for (int i = 0; i < view.rowCount(); ++i)
            if (view.model()->index(i, 0).data().toString().normalized(
                    QString::NormalizationForm_C) == name)
                return i;
        return -1;
    }
    static FileTable *openPanel(MainWindow &window, unsigned pane, const QString &path) {
        auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
        auto *address = panel->findChild<QLineEdit *>("archivePath");
        PanelKeys::focusAddress(address);
        address->setText(path);
        QTest::keyClick(address, Qt::Key_Return);
        return static_cast<FileTable *>(panel->findChild<FileTable *>("archiveTable"));
    }
    static void openView(MainWindow &window) {
        auto *view = window.findChild<QMenu *>("viewMenu");
        view->popup(window.mapToGlobal(QPoint(20, 40)));
        QApplication::processEvents();
        QVERIFY(view->isVisible());
    }

  private slots:
    void viewSortCommandsReorderArchiveAndSurviveRefresh() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ControlledBackend backend;
        MainWindow window(&backend);
        QList<ArchiveEntry> entries = {{"z.bin", 30, 1}, {"a.txt", 10, 1}, {"c.bin", 20, 1}};
        for (qsizetype index = 0; index < entries.size(); ++index) {
            NWindows::NCOM::CPropVariant time;
            time.vt = VT_FILETIME;
            time.filetime = FILETIME{0, UInt32(0x01D90003 - index)};
            entries[index].modifiedTime = time;
        }
        openListedArchive(window, backend, temp.filePath("sort.7z"), entries);
        auto names = [&] {
            QStringList result;
            for (int row = 0; row < table(window)->rowCount(); ++row)
                result << table(window)->model()->index(row, 0).data().toString();
            return result;
        };
        QCOMPARE(names(), (QStringList{"a.txt", "c.bin", "z.bin"}));
        action(window, "sort1Action")->trigger();
        QCOMPARE(names(), (QStringList{"c.bin", "z.bin", "a.txt"}));
        action(window, "sort2Action")->trigger();
        QCOMPARE(names(), (QStringList{"z.bin", "a.txt", "c.bin"}));
        action(window, "sort3Action")->trigger();
        QCOMPARE(names(), (QStringList{"z.bin", "c.bin", "a.txt"}));
        QTest::keyClick(table(window), Qt::Key_F7, Qt::ControlModifier);
        QCOMPARE(names(), (QStringList{"z.bin", "c.bin", "a.txt"}));
        action(window, "sort0Action")->trigger();
        QCOMPARE(names(), (QStringList{"a.txt", "c.bin", "z.bin"}));
        backend.queueList(entries);
        action(window, "refreshAction")->trigger();
        QCOMPARE(names(), (QStringList{"a.txt", "c.bin", "z.bin"}));
        QVERIFY(action(window, "sort0Action")->isChecked());

        CListViewInfo oldSort;
        oldSort.SortID = kpidNoProperty;
        oldSort.Ascending = false;
        oldSort.Save(table(window)->sourcePanel->GetFolderTypeID(),
                     qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ControlledBackend reopenedBackend;
        MainWindow reopened(&reopenedBackend);
        openListedArchive(reopened, reopenedBackend, temp.filePath("sort.7z"), entries);
        const auto *model = table(reopened)->model();
        QCOMPARE(model->index(0, 0).data().toString(), QString("a.txt"));
        QCOMPARE(model->index(2, 0).data().toString(), QString("z.bin"));
        QVERIFY(action(reopened, "sort0Action")->isChecked());
    }

    void openMenuKeepsItsSourcePanel() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        for (const auto &[name, size] :
             {std::pair{"a-small.txt", 2}, std::pair{"z-large.txt", 100}}) {
            QFile file(fixture.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(size, 'x')), qint64(size));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1, fixture.path());
        QTRY_VERIFY(peer->isEnabled() && peer->rowCount() == 2);
        auto *source = openPanel(window, 0, fixture.path());
        QTRY_VERIFY(source->isEnabled() && source->rowCount() == 2);
        source->setFocus();
        QTRY_VERIFY(source->hasFocus());
        source->setCurrentCell(0, 0);
        source->selectRow(0);
        action(window, "sort2Action")->trigger();
        openView(window);
        auto *view = window.findChild<QMenu *>("viewMenu");
        peer->selectRow(0);
        QVERIFY(view->isVisible());
        QVERIFY(action(window, "sort2Action")->isChecked());
        QVERIFY(!action(window, "sort0Action")->isChecked());
        auto *sort = window.findChild<QMenu *>("sortMenu");
        view->setActiveAction(sort->menuAction());
        QTest::keyClick(view, Qt::Key_Right);
        QTRY_VERIFY(sort->isVisible());
        auto *size = action(window, "sort3Action");
        QTest::mouseClick(sort, Qt::LeftButton, Qt::NoModifier,
                          sort->actionGeometry(size).center());
        QTRY_VERIFY(!view->isVisible());
        QCOMPARE(source->model()->index(0, 0).data().toString(), QString("z-large.txt"));
        QCOMPARE(peer->model()->index(0, 0).data().toString(), QString("a-small.txt"));
        QCOMPARE(source->horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);
        QCOMPARE(peer->horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);
        QTRY_VERIFY(source->hasFocus());
        QCOMPARE(source->currentIndex().data().toString(), QString("a-small.txt"));
        QTest::keyClick(window.focusWidget(), Qt::Key_F2);
        QTRY_VERIFY(source->findChild<QLineEdit *>());
        auto *editor = source->findChild<QLineEdit *>();
        QCOMPARE(editor->text(), QString("a-small.txt"));
        QVERIFY(!peer->findChild<QLineEdit *>());
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_VERIFY(source->hasFocus());
    }
    void activationAndCommands_data() {
        QTest::addColumn<unsigned>("pane");
        QTest::addColumn<QString>("language");
        for (unsigned pane : {0u, 1u})
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(qPrintable(QString("pane%1-%2").arg(pane).arg(language)))
                    << pane << language;
    }
    void activationAndCommands() {
        QFETCH(unsigned, pane);
        QFETCH(QString, language);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString name = QStringLiteral("файл δ with space.txt");
        QFile file(fixture.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("protected menu fixture"), qint64(22));
        file.close();
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - pane, fixture.path());
        auto *source = openPanel(window, pane, fixture.path());
        QTRY_VERIFY(row(*source, name) >= 0 && row(*peer, name) >= 0);
        source->setCurrentCell(row(*source, name), 0);
        source->selectRow(row(*source, name));
        source->setFocus();
        auto *provider = source->sourcePanel->GetFolder();
        auto *peerProvider = peer->sourcePanel->GetFolder();
        auto *view = window.findChild<QMenu *>("viewMenu");
        const auto peerMode = peer->sourcePanel->GetListViewMode();
        openView(window);
        auto *appearance = view->findChild<QMenu *>("appearanceMenu");
        QVERIFY(appearance);
        view->setActiveAction(appearance->menuAction());
        QTest::keyClick(view, Qt::Key_Right);
        QApplication::processEvents();
        QVERIFY(appearance->isVisible());
        appearance->setActiveAction(action(window, "viewMode0Action"));
        QTest::keyClick(appearance, Qt::Key_Return);
        QCOMPARE(source->sourcePanel->GetListViewMode(), 0u);
        QCOMPARE(peer->sourcePanel->GetListViewMode(), peerMode);
        action(window, "sort3Action")->trigger();
        source->sourcePanel->SortItemsWithPropID(kpidCTime);
        openView(window);
        QVERIFY(!action(window, "sort0Action")->isChecked());
        QVERIFY(!action(window, "sort3Action")->isChecked());
        action(window, "selectAllAction")->trigger();
        QCOMPARE(source->sourcePanel->GetSortID(), PROPID(kpidCTime));
        QVERIFY(!action(window, "sort0Action")->isChecked());
        QVERIFY(!action(window, "sort3Action")->isChecked());
        QVERIFY(action(window, "viewMode0Action")->isChecked());
        QVERIFY(action(window, "twoPanelsAction")->isChecked());
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            view->hide();
            window.resize(size);
            openView(window);
            const QString tag = QString("view-menu-%1-%2x%3")
                                    .arg(QTest::currentDataTag())
                                    .arg(size.width())
                                    .arg(size.height());
            for (unsigned index : {0u, 1u}) {
                auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(index));
                auto *list = panel->findChild<QWidget *>("archiveTable");
                if (!list->isVisible())
                    list = panel->findChild<QWidget *>("iconList");
                QVERIFY(UiInspection::fullyVisible(*list, window));
                QVERIFY(UiInspection::fullyVisible(
                    *PanelKeys::navigationFor(panel->findChild<QWidget *>("archivePath")), window));
            }
            QVERIFY(UiInspection::writeReport(window, tag + "-main"));
            QVERIFY(UiInspection::writeReport(*view, tag));
            view->hide();
            for (const char *name : {"appearanceMenu", "sortMenu"}) {
                auto *sub = view->findChild<QMenu *>(name);
                QVERIFY(sub);
                sub->popup(window.mapToGlobal(QPoint(30, 50)));
                QApplication::processEvents();
                for (auto *entry : sub->actions())
                    if (entry->isVisible())
                        QVERIFY(sub->rect().contains(sub->actionGeometry(entry)));
                QVERIFY(UiInspection::writeReport(*sub, tag + '-' + sub->objectName()));
                sub->hide();
            }
        }
        QCOMPARE(source->sourcePanel->GetFolder(), provider);
        QCOMPARE(peer->sourcePanel->GetFolder(), peerProvider);
        QVERIFY(source->sourcePanel->ShouldProcessNotifications());
        QVERIFY(peer->sourcePanel->ShouldProcessNotifications());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("protected menu fixture"));
    }
};

#include "ui/support/review_test_main.h"
SEVENZIP_UI_TEST_MAIN(ViewMenuTests)
#include "view_menu_tests.moc"
