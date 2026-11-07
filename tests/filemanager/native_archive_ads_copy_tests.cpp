#include "7zip/UI/GUI/ExtractRes.h"

#include "archive/agent/Agent.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/copy/alias.h"
#include "dialogs/delete/qtdeleteconfirmation.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "support/panel_keys.h"
#include "support/progress_dialog.h"
#include "ui/support/layout_inspection.h"
#include <QScopeGuard>
#include <QScrollArea>

using namespace SourceExtractFixture;

class NativeArchiveAdsCopyTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    QString module_;
    quint32 version_ = 0;
    enum Outcome { Copy, DialogCancel, Skip, OverwriteCancel, UnsupportedMove };
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
    static void inspect(MainWindow &window, const QString &tag) {
        for (const QSize size : {QSize(980, 620), QSize(640, 400)}) {
            window.resize(size);
            QApplication::processEvents();
            for (unsigned pane : {0u, 1u}) {
                auto *panel = window.findChild<QWidget *>(QString("filePanel%1").arg(pane));
                for (const char *name : {"archivePath", "archiveTable"})
                    QVERIFY(UiInspection::fullyVisible(*panel->findChild<QWidget *>(name), window));
            }
            QVERIFY(UiInspection::writeReport(window, QString("native-archive-ads-%1-%2x%3")
                                                          .arg(tag)
                                                          .arg(size.width())
                                                          .arg(size.height())));
        }
    }
    static void transfer(MainWindow &window, FileTable &source, const QString &name,
                         const QString &destination, Outcome outcome, const QString &tag) {
        QVERIFY(row(source, name) >= 0);
        source.clearSelection();
        source.setCurrentCell(row(source, name), 0);
        source.selectRow(row(source, name));
        source.setFocus();
        bool seen = false, overwrite = false, deleteQuestion = false;
        QStringList errors;
        QStringList reports;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &window, [&] {
            if (auto *dialog = window.findChild<CopyDialog *>("copyDialog");
                dialog && dialog->isVisible() && !seen) {
                seen = true;
                const auto close = qScopeGuard([&] {
                    if (dialog->isVisible())
                        dialog->reject();
                });
                QCOMPARE(QDir::fromNativeSeparators(dialog->destination()),
                         QDir::fromNativeSeparators(destination));
                for (const QSize size : {QSize(440, 250), QSize(340, 220)}) {
                    dialog->resize(size);
                    dialog->layout()->activate();
                    auto *scroll = dialog->findChild<QScrollArea *>("dialogContentScroll");
                    QVERIFY(scroll);
                    for (const char *control :
                         {"copyDestination", "copyBrowseButton", "copySourceSummary"}) {
                        auto *widget = dialog->findChild<QWidget *>(control);
                        QVERIFY(widget);
                        const auto reveal = [&] {
                            const QRect bounds(widget->mapTo(scroll->widget(), QPoint()),
                                               widget->size());
                            scroll->ensureVisible(bounds.left(), bounds.top(), 0, 0);
                            scroll->ensureVisible(bounds.right() + 1, bounds.bottom() + 1, 0, 0);
                            return UiInspection::fullyVisible(*widget, *dialog);
                        };
                        QTRY_VERIFY2(reveal(), control);
                        QVERIFY(UiInspection::fullyVisible(
                            *dialog->findChild<QWidget *>("copyButtons"), *dialog));
                    }
                    QVERIFY(UiInspection::writeReport(*dialog,
                                                      QString("native-archive-ads-copy-%1-%2x%3")
                                                          .arg(tag)
                                                          .arg(size.width())
                                                          .arg(size.height())));
                }
                outcome == DialogCancel ? dialog->reject() : dialog->accept();
            }
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (!widget->isVisible())
                    continue;
                if (auto *result = TestProgressDialog::ResultWindow(widget)) {
                    const QString report = TestProgressDialog::ReportText(*result);
                    if (!report.isEmpty())
                        reports << report;
                    TestProgressDialog::CloseResult(*result);
                    continue;
                }
                if (auto *confirmation = dynamic_cast<QtDeleteConfirmation *>(widget);
                    confirmation && confirmation->isVisible()) {
                    deleteQuestion = true;
                    confirmation->reject();
                }
                if (widget->objectName() == "overwriteDialog" && widget->isVisible()) {
                    overwrite = true;
                    widget
                        ->findChild<QPushButton *>(outcome == Skip ? "overwriteNo"
                                                                   : "overwriteCancel")
                        ->click();
                }
                if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
                    deleteQuestion |= box->button(QMessageBox::Yes) != nullptr;
                    errors << box->text();
                    box->reject();
                }
            }
        });
        driver.start(1);
        QTest::keyClick(&source, outcome == UnsupportedMove ? Qt::Key_F6 : Qt::Key_F5);
        QTRY_VERIFY(action(window, "refreshAction")->isEnabled());
        driver.stop();
        QVERIFY(seen);
        QCOMPARE(overwrite, outcome == Skip || outcome == OverwriteCancel);
        QVERIFY(!deleteQuestion);
        if (outcome == UnsupportedMove) {
            QVERIFY(!errors.isEmpty() || !reports.isEmpty());
        } else {
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join('\n')));
            const QString report = reports.join('\n');
            if (outcome == Skip) {
                QVERIFY(report.normalized(QString::NormalizationForm_C).contains(name));
                QVERIFY(report.contains(ToQString(LangString(IDS_PROGRESS_SKIPPING))));
            } else {
                QVERIFY2(report.isEmpty(), qPrintable(report));
            }
        }
    }

  private slots:
    void initTestCase() {
#if !defined(Q_OS_WIN) || !defined(SEVENZIP_CODEC_MODULE)
        QSKIP("Actual archive-to-NTFS Copy requires Windows and the official codec module.");
#else
        module_ = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        ArchiveCodecCatalog catalog(module_, 0);
        QCOMPARE(catalog.result(), S_OK);
        version_ = catalog.version();
#endif
    }
    void sourceCopyAndRefusal_data() {
        QTest::addColumn<unsigned>("sourcePane");
        QTest::addColumn<QString>("language");
        for (unsigned pane : {0u, 1u})
            for (const QString &language : {QString("-"), QString("ru")})
                QTest::newRow(qPrintable(QString("pane%1-%2").arg(pane).arg(language)))
                    << pane << language;
    }
    void sourceCopyAndRefusal() {
        QFETCH(unsigned, sourcePane);
        QFETCH(QString, language);
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QByteArray payload("native archive\0ADS payload", 26);
        const QString archive = makeArchive(fixture.filePath("source with space"), payload);
        QVERIFY(!archive.isEmpty());
        const QByteArray archiveBefore = readFile(archive);
        const QString name = QFileInfo(kName).fileName();
        const QString base = fixture.filePath(QStringLiteral("основной файл δ.bin"));
        const QString target = base + ':' + name;
        QVERIFY(writeFile(base, "protected base"));
        QVERIFY(writeFile(base + ":control", "protected peer stream"));
        ArchiveBackend backend;
        QCOMPARE(backend.loadCodecCatalog(module_, version_), S_OK);
        MainWindow window(&backend);
        window.show();
        if (!action(window, "twoPanelsAction")->isChecked())
            action(window, "twoPanelsAction")->trigger();
        auto *peer = openPanel(window, 1 - sourcePane, base + ':');
        QTRY_VERIFY(row(*peer, "control") >= 0);
        auto *source = openPanel(window, sourcePane, archive + "/inside/");
        QTRY_VERIFY(row(*source, name) >= 0);
        CMyComPtr<IFolderFolder> sourceProvider = source->sourcePanel->GetFolder();
        CMyComPtr<IFolderFolder> peerProvider = peer->sourcePanel->GetFolder();
        QVERIFY(dynamic_cast<CAgentFolder *>(sourceProvider.Interface()));
        QVERIFY(peer->sourcePanel->IsAltStreamsFolder());
        for (Outcome outcome : {DialogCancel, Skip, OverwriteCancel, Copy, UnsupportedMove}) {
            if (outcome == Skip || outcome == OverwriteCancel)
                QVERIFY(writeFile(target, "occupied stream"));
            else if (QFileInfo::exists(target))
                QVERIFY(QFile::remove(target));
            peer->sourcePanel->OnReload();
            const QString tag = QString("%1-%2").arg(QTest::currentDataTag()).arg(outcome);
            transfer(window, *source, name, base + ':', outcome, tag);
            if (QTest::currentTestFailed())
                return;
            QCOMPARE(readFile(archive), archiveBefore);
            QCOMPARE(readFile(base), QByteArray("protected base"));
            QCOMPARE(readFile(base + ":control"), QByteArray("protected peer stream"));
            QCOMPARE(source->sourcePanel->GetFolder(), sourceProvider.Interface());
            QCOMPARE(peer->sourcePanel->GetFolder(), peerProvider.Interface());
            QTRY_VERIFY(row(*source, name) >= 0);
            if (outcome == Copy) {
                QCOMPARE(readFile(target), payload);
                QTRY_VERIFY(row(*peer, name) >= 0);
            } else if (outcome == Skip || outcome == OverwriteCancel)
                QCOMPARE(readFile(target), QByteArray("occupied stream"));
            else
                QVERIFY(!QFileInfo::exists(target));
            QCOMPARE(source->currentRow(), row(*source, name));
            QVERIFY(source->sourcePanel->ShouldProcessNotifications());
            QVERIFY(peer->sourcePanel->ShouldProcessNotifications());
            QVERIFY(source->isEnabled() && peer->isEnabled());
            inspect(window, tag);
        }
    }
};

QTEST_MAIN(NativeArchiveAdsCopyTests)
#include "native_archive_ads_copy_tests.moc"
