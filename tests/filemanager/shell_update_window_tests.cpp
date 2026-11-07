#include "Common/MyTypes.h"
#include "filemanager/views/filetable.h"
#include "support/archive_sections.h"

#include "7zip/UI/Explorer/ContextMenuFlags.h"
#include "7zip/UI/FileManager/ProgressDialog2Res.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/support/source_extract_fixture.h"
#include "dialogs/compress/createarchivedialog.h"
#include "filemanager/support/window_state_support.h"
#include "support/panel_keys.h"
#include "support/widget_dialogs.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

namespace {
QAction *findVerb(QMenu &menu, const QString &verb) {
    for (auto *action : menu.actions()) {
        if (action->property("shellVerb").toString() == verb)
            return action;
        if (action->menu())
            if (auto *found = findVerb(*action->menu(), verb))
                return found;
    }
    return nullptr;
}
struct CCurrentDirectory {
    const QString Path = QDir::currentPath();
    ~CCurrentDirectory() { QDir::setCurrent(Path); }
};
int rowNamed(MainWindow &window, const QString &name) {
    auto *view = window.findChild<FileTable *>("archiveTable");
    for (int row = 0; row < view->rowCount(); ++row)
        if (view->model()->index(row, 0).data().toString() == name)
            return row;
    return -1;
}
void prepareSettings(const QString &language = "-") {
    const QString settings = qEnvironmentVariable("SEVENZIP_SETTINGS_PATH");
    QFile::remove(settings);
    SaveRegLang(ToUString(language), settings);
    CContextMenuInfo ci;
    ci.Flags = NContextMenuFlags::kCompress | NContextMenuFlags::kCompressTo7z |
               NContextMenuFlags::kCompressToZip | NContextMenuFlags::kCRC_Cascaded;
    ci.Flags_Def = true;
    ci.Cascaded.SetTrueTrue();
    ci.MenuIcons.SetTrueTrue();
    ci.Save(settings);
}
} // namespace

class ShellUpdateWindowTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    WidgetDialogFixture dialogs_;
    QString _module, _engine;
    quint32 _version = 0;
  private slots:
    void initTestCase() {
#ifndef SEVENZIP_CODEC_MODULE
        QSKIP("Official native runtime unavailable");
#else
        _module = QString::fromUtf8(SEVENZIP_CODEC_MODULE);
        _engine = QStandardPaths::findExecutable("7z");
        QVERIFY(!_engine.isEmpty());
        ArchiveCodecCatalog probe(_module, 0);
        QCOMPARE(probe.result(), S_OK);
        _version = probe.version();
#endif
    }
    void popupAndToolbarPassBoundSelectionToOperationGui_data() {
        QTest::addColumn<QString>("type");
        for (const QString &type : {"7z", "zip", "modal", "toolbar", "program-7z", "program-modal"})
            QTest::newRow(qPrintable(type)) << type;
    }
    void popupAndToolbarPassBoundSelectionToOperationGui() {
        QFETCH(QString, type);
        QTemporaryDir fixture;
        CCurrentDirectory guard;
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        const QString source = fixture.filePath(name);
        QVERIFY(SourceExtractFixture::writeFile(source, "abc"));
        QVERIFY(QDir::setCurrent(fixture.path()));
        prepareSettings();
        ControlledBackend backend;
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        MainWindow window(&backend);
        window.show();
        auto *view = table(window);
        QTRY_VERIFY(rowNamed(window, name) >= 0);
        QTRY_VERIFY(view->isEnabled());
        view->setCurrentCell(rowNamed(window, name), 0);
        view->selectRow(rowNamed(window, name));
        const bool toolbar = type == "toolbar";
        const bool modal = type.endsWith("modal") || toolbar;
        const bool program = type.startsWith("program-");
        const QString verb = modal           ? "SevenZipCompress"
                             : type == "zip" ? "SevenZipCompressToZip"
                                             : "SevenZipCompressTo7z";
        if (toolbar) {
            QVERIFY(action(window, "toolbarAddAction")->isEnabled());
            view->setFocus();
            PanelKeys::command(view, Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        } else if (program) {
            auto *menu = window.findChild<QMenu *>("fileMenu");
            QVERIFY(menu);
            QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow", Qt::DirectConnection));
            auto *command = findVerb(*menu, verb);
            QVERIFY(command && command->isEnabled());
            command->trigger();
            command->trigger(); // A released menu command must be safe to invoke again.
        } else {
            bool invoked = false;
            QTimer::singleShot(0, &window, [&] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                auto *command = findVerb(*menu, verb);
                QVERIFY(command && command->isEnabled());
                menu->close();
                invoked = true;
                command->trigger();
            });
            QVERIFY(QMetaObject::invokeMethod(
                view, "customContextMenuRequested", Qt::DirectConnection,
                Q_ARG(QPoint,
                      view->visualRect(view->model()->index(view->currentRow(), 0)).center())));
            QVERIFY(invoked);
        }
        QTRY_COMPARE(backend.GuiRequests.size(), 1);
        const auto &job = backend.GuiRequests.first();
        QCOMPARE(job.Operation, QtOperationJob::Command::Compress);
        QCOMPARE(job.ShowDialog, modal);
        if (!modal)
            QCOMPARE(job.ArchiveType, type == "zip" ? QString("zip") : QString("7z"));
        QCOMPARE(job.Paths.size(), 1);
        const QDir base(fixture.path());
        QCOMPARE(QDir::cleanPath(base.absoluteFilePath(job.Paths.first()))
                     .normalized(QString::NormalizationForm_C),
                 source.normalized(QString::NormalizationForm_C));
        QTRY_VERIFY(action(window, "toolbarAddAction")->isEnabled());
        QCOMPARE(view->selectionModel()->selectedRows().size(), toolbar ? 1 : 0);
        QCOMPARE(SourceExtractFixture::readFile(source), QByteArray("abc"));
        QVERIFY(!backend.busy());
    }

    void updateCommandGeometry_data() {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QSize>("size");
        for (const QString &language : {QString("-"), QString("ru")})
            for (const QSize &size : {QSize(980, 620), QSize(640, 400)})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language).arg(size.width())))
                    << language << size;
    }
    void updateCommandGeometry() {
        QFETCH(QString, language);
        QFETCH(QSize, size);
        QTemporaryDir fixture;
        CCurrentDirectory guard;
        const QString name = QStringLiteral("данные 空 with spaces.bin");
        QVERIFY(SourceExtractFixture::writeFile(fixture.filePath(name), "abc"));
        QVERIFY(QDir::setCurrent(fixture.path()));
        prepareSettings(language);
        ArchiveBackend backend(nullptr, _engine);
        QCOMPARE(backend.loadCodecCatalog(_module, _version), S_OK);
        MainWindow window(&backend);
        window.resize(size);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *view = table(window);
        QTRY_VERIFY(rowNamed(window, name) >= 0);
        view->selectRow(rowNamed(window, name));
        bool inspected = false;
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto *root = menu->findChild<QAction *>("sevenZipContextGroup");
            QVERIFY(root && root->menu());
            auto *compress = findVerb(*menu, "SevenZipCompress");
            auto *sevenZip = findVerb(*menu, "SevenZipCompressTo7z");
            auto *zip = findVerb(*menu, "SevenZipCompressToZip");
            auto *checksum = findVerb(*menu, "SevenZip.Checksum.Calc.SHA256");
            QVERIFY(compress && sevenZip && zip && checksum);
            QVERIFY(compress->isEnabled());
            QVERIFY(sevenZip->isEnabled() && zip->isEnabled() && checksum->isEnabled());
            QMenu *child = root->menu();
            child->popup(menu->mapToGlobal(menu->actionGeometry(root).topRight()));
            QCoreApplication::processEvents();
            const QString tag =
                QString("shell-update-%1-%2").arg(language == "ru" ? "ru" : "en").arg(size.width());
            for (auto *action : child->actions()) {
                const QRect rect = child->actionGeometry(action);
                QVERIFY(child->rect().contains(rect));
                if (!action->isSeparator())
                    QVERIFY(child->fontMetrics().horizontalAdvance(action->text()) < rect.width());
            }
            QVERIFY(UiInspection::writeReport(*child, tag));
            QMenu *hashMenu = nullptr;
            for (auto *action : child->actions())
                if (action->menu() && action->menu()->actions().contains(checksum))
                    hashMenu = action->menu();
            QVERIFY(hashMenu);
            hashMenu->popup(
                child->mapToGlobal(child->actionGeometry(hashMenu->menuAction()).topRight()));
            QCoreApplication::processEvents();
            for (auto *action : hashMenu->actions()) {
                QVERIFY(hashMenu->rect().contains(hashMenu->actionGeometry(action)));
                if (!action->isSeparator())
                    QVERIFY(hashMenu->fontMetrics().horizontalAdvance(action->text()) <
                            hashMenu->actionGeometry(action).width());
            }
            QVERIFY(UiInspection::writeReport(*hashMenu, tag + "-hash"));
            hashMenu->close();
            child->close();
            menu->close();
            inspected = true;
        });
        QVERIFY(QMetaObject::invokeMethod(
            view, "customContextMenuRequested", Qt::DirectConnection,
            Q_ARG(QPoint, view->visualRect(view->model()->index(view->currentRow(), 0)).center())));
        QVERIFY(inspected);
    }
};
QTEST_MAIN(ShellUpdateWindowTests)
#include "shell_update_window_tests.moc"
