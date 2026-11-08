#include "dialogs/copy/CopyDialog.h"
#include "filemanager/App.h"
#include "localization/LangUtils.h"
#include "settings/ViewSettings.h"
#include "ui/progress/ProgressDialog2.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

namespace {
class CopyPresentation final : public IQtAppPresentation {
  public:
    QTableWidget *views[2];
    unsigned executions = 0;
    QString destination;
    QtCopyRoute route = QtCopyRoute::FileSystem;
    CRecordVector<UInt32> copiedIndices;
    CApp *owner = nullptr;
    bool beginOperation = false;
    explicit CopyPresentation(QWidget &window) {
        for (auto &view : views)
            view = new QTableWidget(&window);
    }
    void Initialize(CApp &app) override { owner = &app; }
    bool CreatePanel(CPanel &panel, unsigned index) override {
        if (panel.GetFolder()->LoadItems() != S_OK)
            return false;
        panel.LoadFullPath();
        UInt32 count = 0;
        if (panel.GetFolder()->GetNumberOfItems(&count) != S_OK)
            return false;
        QTableWidget &view = *views[index];
        view.setColumnCount(1);
        view.setRowCount(int(count));
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
        view.setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (UInt32 i = 0; i < count; ++i) {
            auto *item = new QTableWidgetItem(ToQString(panel.GetItemName(i)));
            item->setData(Qt::UserRole + 6, i);
            view.setItem(int(i), 0, item);
        }
        panel.AttachSelectionView(&view);
        return true;
    }
    CPanelCallback *PanelCallback(unsigned) override { return nullptr; }
    void SetListSettings(unsigned, const CFmSettings &) override {}
    void ShowPanel(unsigned, bool) override {}
    void ExecuteCopy(bool, bool, unsigned, const QString &path,
                     const CRecordVector<UInt32> &indices, QtCopyRoute selectedRoute) override {
        ++executions;
        destination = path;
        route = selectedRoute;
        copiedIndices = indices;
        if (beginOperation)
            owner->QtOperations().Begin(QtAppOperationState::Kind::Operation);
    }
};

struct CopyInteraction {
    bool shown = false;
    QString error;
    QString initialDestination;
};

CopyInteraction chooseDestination(CApp &app, QWidget &window, const QString &path,
                                  bool cancel = false) {
    CopyInteraction result;
    QTimer driver;
    QObject::connect(&driver, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<CCopyDialog *>("copyDialog");
        if (dialog && dialog->isVisible() && !result.shown) {
            result.shown = true;
            auto *destination = dialog->findChild<QLineEdit *>("copyDestination");
            result.initialDestination = destination->text();
            destination->setText(path);
            if (cancel)
                dialog->reject();
            else {
                dialog->accept();
                if (dialog->isVisible()) {
                    auto *label = dialog->findChild<QLabel *>("copyDestinationMessage");
                    if (label && label->isVisible())
                        result.error = label->text();
                    dialog->reject();
                }
            }
        }
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *message = qobject_cast<QMessageBox *>(widget))
                if (message->isVisible()) {
                    result.error = message->text();
                    message->accept();
                }
    });
    driver.start(0);
    app.OnCopy(false, false, 0);
    return result;
}
} // namespace

class AppCopyRoutingTests final : public QObject {
    Q_OBJECT
    static void write(const QString &path) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("control bytes"), qint64(13));
    }
    static void create(CApp &app, CopyPresentation &presentation, QWidget &window,
                       const QString &source, const QString &peer, const QString &settings) {
        SavePanelPath(1, ToUString(peer), settings);
        COpenResult opened;
        int widths[2] = {300, 300};
        QCOMPARE(app.Create(&window, ToUString(source), UString(), widths, false, opened,
                            presentation, settings),
                 S_OK);
        QVERIFY(app.Panels[0].IsFSFolder());
        if (app.NumPanels == 2)
            QVERIFY(app.Panels[1].IsFSFolder());
        presentation.views[0]->selectAll();
    }
  private slots:
    void copyDefaultsToOtherPanelAfterCancelAndAccept() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("source 空"));
        const QString peer = fixture.filePath("K");
        const QString settings = fixture.filePath("settings.ini");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        const QString payload = QDir(source).filePath("one.txt");
        write(payload);
        QWidget window;
        CopyPresentation presentation(window);
        CApp app;
        create(app, presentation, window, source, peer, settings);
        const auto cancelled = chooseDestination(app, window, peer + '/', true);
        QVERIFY(cancelled.shown);
        QVERIFY(cancelled.error.isEmpty());
        QCOMPARE(QDir::cleanPath(cancelled.initialDestination), peer);
        QCOMPARE(presentation.executions, 0u);
        const auto accepted = chooseDestination(app, window, peer + '/');
        QVERIFY(accepted.shown);
        QVERIFY(accepted.error.isEmpty());
        QCOMPARE(QDir::cleanPath(accepted.initialDestination), peer);
        QCOMPARE(presentation.executions, 1u);
        app.CompleteCopy();
        presentation.views[0]->selectAll();
        const auto reopened = chooseDestination(app, window, peer + '/', true);
        QVERIFY(reopened.shown);
        QVERIFY(reopened.error.isEmpty());
        QCOMPARE(QDir::cleanPath(reopened.initialDestination), peer);
        QCOMPARE(presentation.executions, 1u);
        QFile bytes(payload);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        QCOMPARE(bytes.readAll(), QByteArray("control bytes"));
        QVERIFY(QDir(peer).entryList(QDir::Files).isEmpty());
    }
    void asynchronousCopyRetainsThenRestoresSourceTimer_data() {
        QTest::addColumn<unsigned>("panels");
        QTest::newRow("one-panel") << 1u;
        QTest::newRow("two-panels") << 2u;
    }
    void asynchronousCopyRetainsThenRestoresSourceTimer() {
        QFETCH(unsigned, panels);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        write(QDir(source).filePath("one.txt"));
        QWidget window;
        CopyPresentation presentation(window);
        CApp app;
        app.NumPanels = panels;
        create(app, presentation, window, source, peer, fixture.filePath("settings.ini"));
        unsigned reloads = 0;
        CPanel &panel = app.Panels[0];
        panel.Set_AutoRefresh_Mode(true);
        panel.AttachReloadPresentation([&](const CSelectedState &) {
            ++reloads;
            return S_OK;
        });
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QCOMPARE(reloads, 1u);
        presentation.beginOperation = true;
        const auto interaction = chooseDestination(app, window, peer + '/');
        QVERIFY(interaction.shown);
        QVERIFY(interaction.error.isEmpty());
        QCOMPARE(presentation.executions, 1u);
        QVERIFY(app.QtOperations().blocking());
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        const unsigned whileOperating = reloads;
        app.QtOperations().End();
        panel.MarkPortableFolderChanged();
        panel.OnTimer();
        QCOMPARE(whileOperating, 1u);
        QCOMPARE(reloads, 2u);
        UStringVector beforeCompletion, afterCompletion;
        panel.AttachReloadPresentation([&](const CSelectedState &selection) {
            beforeCompletion = selection.SelectedNames;
            app.CompleteCopy();
            afterCompletion = selection.SelectedNames;
            return S_OK;
        });
        QVERIFY(app.RefreshCopyPanel(0));
        QCOMPARE(beforeCompletion.Size(), 1u);
        QCOMPARE(afterCompletion.Size(), 1u);
        QCOMPARE(ToQString(beforeCompletion[0]), QString("one.txt"));
        QCOMPARE(ToQString(afterCompletion[0]), ToQString(beforeCompletion[0]));
        QVERIFY(!app.RefreshCopyPanel(0));
        QVERIFY(presentation.views[0]->selectionModel()->selectedRows().isEmpty());
        QVERIFY(panel.ShouldProcessNotifications());
        presentation.views[0]->selectRow(0);
        app.CompleteCopy();
        QCOMPARE(presentation.views[0]->selectionModel()->selectedRows().size(), 1);
    }
    void zeroErrorUsesSourceFailureMessage() {
        CPanel panel;
        QString shown;
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) {
                shown = box->text();
                box->accept();
            }
        });
        panel.MessageBox_Error_HRESULT(0);
        QCOMPARE(shown, ToQString(HResultToMessage(E_FAIL)));
    }
    void filesystemPeerUsesSourceCopyTo() {
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath(QStringLiteral("источник δ"));
        const QString peer = fixture.filePath(QStringLiteral("peer space"));
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        write(QDir(source).filePath("one.txt"));
        QWidget window;
        CopyPresentation presentation(window);
        CApp app;
        create(app, presentation, window, source, peer, fixture.filePath("settings.ini"));
        const auto interaction =
            chooseDestination(app, window, ToQString(app.Panels[1].GetFsPath()));
        QVERIFY(interaction.shown);
        QVERIFY2(interaction.error.isEmpty(), qPrintable(interaction.error));
        QCOMPARE(presentation.executions, 1u);
        QCOMPARE(presentation.route, QtCopyRoute::FileSystem);
        QCOMPARE(presentation.copiedIndices.Size(), 1u);
        QVERIFY(QDir(peer).entryList(QDir::Files).isEmpty());
    }

    void multipleItemsNormalizeAndCreateDirectory_data() {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("accept") << false;
        QTest::newRow("cancel") << true;
    }
    void multipleItemsNormalizeAndCreateDirectory() {
        QFETCH(bool, cancel);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        write(QDir(source).filePath(QStringLiteral("файл one.txt")));
        write(QDir(source).filePath(QStringLiteral("文件 two.txt")));
        QWidget window;
        CopyPresentation presentation(window);
        CApp app;
        create(app, presentation, window, source, peer, fixture.filePath("settings.ini"));
        const QString output = fixture.filePath(QStringLiteral("новая папка/child"));
        const auto interaction = chooseDestination(app, window, output, cancel);
        QVERIFY(interaction.shown);
        QVERIFY2(interaction.error.isEmpty(), qPrintable(interaction.error));
        QCOMPARE(presentation.executions, cancel ? 0u : 1u);
        QCOMPARE(QFileInfo(output).isDir(), !cancel);
        if (!cancel) {
            QCOMPARE(presentation.destination, output + '/');
            QCOMPARE(presentation.copiedIndices.Size(), 2u);
            QCOMPARE(presentation.route, QtCopyRoute::FileSystem);
        }
        QCOMPARE(QDir(source).entryList(QDir::Files).size(), 2);
    }

    void directoryFailureShowsPathAndStopsExecution_data() {
        QTest::addColumn<int>("count");
        QTest::addColumn<QString>("language");
        for (int count : {1, 2})
            for (const char *language : {"-", "ru"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(count).arg(language)))
                    << count << QString::fromLatin1(language);
    }
    void directoryFailureShowsPathAndStopsExecution() {
        QFETCH(int, count);
        QFETCH(QString, language);
        QTemporaryDir fixture;
        QVERIFY(fixture.isValid());
        const QString source = fixture.filePath("source");
        const QString peer = fixture.filePath("peer");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(peer));
        write(QDir(source).filePath("one.txt"));
        if (count == 2)
            write(QDir(source).filePath("two.txt"));
        const QString blocked = fixture.filePath(QStringLiteral("занято δ"));
        write(blocked);
        QWidget window;
        CopyPresentation presentation(window);
        CApp app;
        const QString settings = fixture.filePath("settings.ini");
        SaveRegLang(ToUString(language), settings);
        ReloadLang(settings);
        create(app, presentation, window, source, peer, settings);
        const auto interaction = chooseDestination(app, window, blocked + "/child/new.txt");
        QVERIFY(interaction.shown);
        QVERIFY(!interaction.error.isEmpty());
        QVERIFY2(QDir::fromNativeSeparators(interaction.error).contains(blocked),
                 qPrintable(interaction.error));
        QCOMPARE(presentation.executions, 0u);
        QVERIFY(QFileInfo(blocked).isFile());
        QVERIFY(!QFileInfo::exists(blocked + "/child"));
    }
};

QTEST_MAIN(AppCopyRoutingTests)
#include "app_copy_routing_tests.moc"
