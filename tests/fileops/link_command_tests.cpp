#include "7zip/PropID.h"
#include "7zip/UI/FileManager/LinkDialogRes.h"
#include "filemanager/support/panel_model_support.h"
#include "filemanager/support/window_state_support.h"
#include <QRadioButton>

class LinkCommandTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static void showLinkColumn(FileTable &view) {
        QString label;
        const auto &columns = view.sourcePanel->GetColumns();
        FOR_VECTOR(i, columns)
        if (columns[i].ID == kpidNtReparse)
            label = ToQString(columns[i].Name);
        QVERIFY(!label.isEmpty());
        bool found = false;
        QTimer::singleShot(0, &view, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (auto *option : menu->actions())
                if (option->text() == label) {
                    if (!option->isChecked())
                        option->trigger();
                    found = true;
                    break;
                }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(view.horizontalHeader(), "customContextMenuRequested",
                                          Qt::DirectConnection, Q_ARG(QPoint, QPoint(10, 5))));
        QVERIFY(found);
    }

  private slots:
    void init() { QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH")); }
    void sourceMenuCreatesLinkAndKeepsSelection_data() {
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("deferred");
        QTest::newRow("create") << false << false;
        QTest::newRow("cancel") << true << false;
#ifdef Q_OS_WIN
        QTest::newRow("deferred-create") << false << true;
        QTest::newRow("deferred-cancel") << true << true;
#endif
    }
    void sourceMenuCreatesLinkAndKeepsSelection() {
        QFETCH(bool, cancel);
        QFETCH(bool, deferred);
        QTemporaryDir temp;
        const QString name = QStringLiteral("исходный файл 空.txt");
        QFile input(temp.filePath(name));
        QVERIFY(input.open(QIODevice::WriteOnly));
        QCOMPARE(input.write("source bytes"), qint64(12));
        input.close();
        if (deferred)
            for (int i = 0; i < 257; ++i) {
                QFile extra(temp.filePath(QString("extra-%1.txt").arg(i)));
                QVERIFY(extra.open(QIODevice::WriteOnly));
            }
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        QTRY_VERIFY(table(window)->isEnabled());
        if (deferred) {
            showLinkColumn(*table(window));
            QVERIFY(!QTest::currentTestFailed());
        }
        if (!action(window, "autoRefreshAction")->isChecked())
            action(window, "autoRefreshAction")->trigger();
        table(window)->selectRow(filesystemRow(window, name));
        auto *link = action(window, "linkAction");
        QTRY_VERIFY(link->isEnabled());
        const QString output = temp.filePath(QStringLiteral("новая ссылка.txt"));
        const QString duringName = QStringLiteral("во время диалога 空.txt");
        bool opened = false;
        QStringList errors;
        QTimer responder;
        connect(&responder, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                if (auto *parent = qobject_cast<QDialog *>(box->parentWidget()))
                    QTimer::singleShot(0, parent, &QDialog::reject);
                box->accept();
            } else if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                       dialog && dialog->objectName() == "IDD_LINK" && !opened) {
                opened = true;
                struct RejectOnFailure {
                    QDialog &dialog;
                    bool armed = true;
                    ~RejectOnFailure() {
                        if (armed)
                            dialog.reject();
                    }
                } guard{*dialog};
                auto *from = dialog->findChild<QComboBox *>("IDC_LINK_PATH_FROM");
                auto *to = dialog->findChild<QComboBox *>("IDC_LINK_PATH_TO");
                QCOMPARE(QDir::fromNativeSeparators(to->currentText())
                             .normalized(QString::NormalizationForm_C),
                         temp.filePath(name).normalized(QString::NormalizationForm_C));
                from->setEditText(output);
                dialog->findChild<QRadioButton *>("IDR_LINK_TYPE_HARD")->setChecked(true);
                if (!deferred) {
                    QFile during(temp.filePath(duringName));
                    QVERIFY(during.open(QIODevice::WriteOnly));
                    during.close();
                }
                QTimer::singleShot(deferred ? 0 : 1200, dialog, [&, dialog] {
                    const bool changed = filesystemRow(window, duringName) >= 0;
                    if (changed)
                        dialog->reject();
                    QVERIFY(!changed);
                    if (cancel)
                        dialog->reject();
                    else
                        dialog->findChild<QPushButton *>("IDB_LINK_LINK")->click();
                });
                guard.armed = false;
            }
        });
        responder.start(1);
        link->trigger();
        responder.stop();
        QVERIFY(opened);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(';')));
        QCOMPARE(QFileInfo::exists(output), !cancel);
        QTRY_VERIFY(table(window)->isEnabled());
        QTRY_VERIFY(link->isEnabled());
        QTRY_VERIFY(filesystemRow(window, name) >= 0);
        QVERIFY(PanelIndexSelected(table(window)->model()->index(filesystemRow(window, name), 0)));
        if (!cancel) {
            QFile appended(output);
            QVERIFY(appended.open(QIODevice::Append));
            QCOMPARE(appended.write("!"), qint64(1));
            appended.close();
            QVERIFY(input.open(QIODevice::ReadOnly));
            QCOMPARE(input.readAll(), QByteArray("source bytes!"));
        }
        QCOMPARE(backend.listCalls, 0);
        QVERIFY(!QApplication::activeModalWidget());
        const QString nextName = QStringLiteral("после диалога 空.txt");
        QFile next(temp.filePath(nextName));
        QVERIFY(next.open(QIODevice::WriteOnly));
        QCOMPARE(next.write("next bytes"), qint64(10));
        next.close();
        QTRY_VERIFY(filesystemRow(window, nextName) >= 0);
    }
    void unavailableForMultipleSelectionAndArchive() {
        QTemporaryDir temp;
        for (const char *name : {"one", "two"}) {
            QFile input(temp.filePath(name));
            QVERIFY(input.open(QIODevice::WriteOnly));
        }
        ControlledBackend backend;
        MainWindow window(&backend);
        auto *address = window.findChild<QLineEdit *>("archivePath");
        address->setText(temp.path());
        QTest::keyClick(address, Qt::Key_Return);
        QTRY_COMPARE(table(window)->rowCount(), 2);
        table(window)->selectAll();
        QVERIFY(!action(window, "linkAction")->isEnabled());
        openListedArchive(window, backend, temp.filePath("fixture.7z"),
                          {ArchiveEntry{"one", 1, 1}});
        table(window)->selectRow(0);
        QVERIFY(!action(window, "linkAction")->isEnabled());
    }
};
QTEST_MAIN(LinkCommandTests)
#include "link_command_tests.moc"
