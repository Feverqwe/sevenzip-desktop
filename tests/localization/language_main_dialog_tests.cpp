#include "7zip/UI/FileManager/resourceGui.h"
#include "filemanager/properties/qtpropertieswindow.h"
#include "filemanager/support/window_state_support.h"
#include "filemanager/views/filetable.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "ui/support/layout_inspection.h"
#include <QScrollArea>

class LanguageMainDialogTests final : public QObject {
    Q_OBJECT
    QTemporaryDir fixture;
    void language(const char *name) {
        const QString path = fixture.filePath("language.ini");
        SaveRegLang(UString(name), path);
        ReloadLang(path);
        QtNotifyLanguageChanged();
    }

  private slots:
    void init() {
        QFile::remove(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        language("-");
    }
    void cleanup() { language("-"); }
    void sourceOpenCancellationUsesSelectedLanguage() {
        for (const char *name : {"-", "ru", "-"}) {
            ControlledBackend backend;
            MainWindow window(&backend);
            language(name);
            const QString archive = fixture.filePath(QStringLiteral("cancel 密碼.zip"));
            QVERIFY(backend.prepareArchiveFixture(archive));
            backend.queueListAction([&] { backend.cancel(); });
            window.openArchive(archive);
            QCOMPARE(backend.lastResult(), E_ABORT);
            QCOMPARE(window.statusBar()->currentMessage(),
                     QString(name) == "ru" ? QStringLiteral("Операция отменена.")
                                           : QString("Operation canceled."));
        }
    }
    void cancellationUsesResultCodeInsteadOfMessageText() {
        ControlledBackend backend;
        MainWindow window(&backend);
        unsigned warningsShown = 0;
        QTimer answer;
        connect(&answer, &QTimer::timeout, &window, [&] {
            if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                ++warningsShown;
                warning->accept();
            }
        });
        answer.start(5);
        for (const char *name : {"-", "ru"}) {
            language(name);
            backend.fail(QStringLiteral("translated cancellation 密碼"), E_ABORT);
            QApplication::processEvents();
            QCOMPARE(warningsShown, 0u);
            QCOMPARE(window.statusBar()->currentMessage(),
                     QStringLiteral("translated cancellation 密碼"));
        }
        backend.fail(QStringLiteral("Операция отменена."), E_FAIL);
        QCOMPARE(warningsShown, 1u);
    }
    void toolbarPropertiesRetainsSourceValuesWithoutBackendReload() {
        ControlledBackend backend;
        MainWindow window(&backend);
        const QString archive = fixture.filePath(QStringLiteral("архив 密碼.zip"));
        QVERIFY(backend.prepareArchiveFixture(archive));
        backend.queueListAction([&] {
            ArchiveInfo info;
            info.format = "zip";
            info.physicalSize = 120;
            info.files = 1;
            info.unpackedSize = 3;
            emit backend.infoReady(info);
            backend.completeList({ArchiveEntry{QStringLiteral("δ space.txt"), 3, 7}});
        });
        window.openArchive(archive);
        window.findChild<FileTable *>("archiveTable")->selectRow(0);
        const int calls = backend.listCalls;
        window.findChild<QAction *>("archiveInfoAction")->trigger();
        auto *dialog = window.findChild<QtPropertiesWindow *>();
        QVERIFY(dialog && dialog->isVisible());
        auto *sizeValue = dialog->findChild<QLabel *>("propertiesSize");
        auto *location = dialog->findChild<QLabel *>("propertiesLocation");
        auto *scroll = dialog->findChild<QScrollArea *>("dialogContentScroll");
        QVERIFY(sizeValue && location && scroll);
        const QString originalSize = sizeValue->text();
        const QString originalLocation = location->text();
        for (const char *name : {"-", "ru", "-"}) {
            language(name);
            const bool russian = QString(name) == "ru";
            QCOMPARE(dialog->windowTitle(),
                     russian ? QStringLiteral("Свойства") : QString("Properties"));
            QVERIFY(dialog->Information().contains(QStringLiteral("δ space.txt")));
            QCOMPARE(sizeValue->text(), originalSize);
            QCOMPARE(location->text(), originalLocation);
            QCOMPARE(backend.listCalls, calls);
            QCOMPARE(backend.extractCalls, 0);
            for (QSize size : {QSize(540, 360), QSize(360, 240)}) {
                dialog->resize(size);
                QApplication::processEvents();
                scroll->ensureWidgetVisible(sizeValue);
                QVERIFY(UiInspection::fullyVisible(*sizeValue, *dialog));
                QVERIFY(UiInspection::writeReport(
                    *dialog,
                    QString("language-toolbar-properties-%1-%2").arg(name).arg(size.width())));
            }
        }
    }
};

QTEST_MAIN(LanguageMainDialogTests)
#include "language_main_dialog_tests.moc"
