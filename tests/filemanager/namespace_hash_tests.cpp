#include "dialogs/taskresult/qttaskresultdialog.h"
#include "filemanager/support/window_state_support.h"
#include "localization/LangUtils.h"
#include "ui/progress/ProgressDialog2.h"
#include "ui/support/layout_inspection.h"
#include <QCryptographicHash>
#include <QPlainTextEdit>
#include <utility>
#ifdef Q_OS_WIN
#include "support/read_only_virtual_disk.h"
#endif

namespace {
class HashModalResponse final : public QObject {
    QTimer timer_;
    QString report_;

  public:
    bool cancel = false;
    bool cancellationRequested = false;
    bool resultSeen = false;
    bool geometry = true;
    QString values;
    QStringList errors;
    explicit HashModalResponse(QString report) : report_(std::move(report)) {
        connect(&timer_, &QTimer::timeout, this, [&] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                errors << box->text();
                box->accept();
                return;
            }
            if (auto *dialog =
                    qobject_cast<QtTaskResultDialog *>(QApplication::activeModalWidget())) {
                resultSeen = true;
                auto *rows = dialog->findChild<QPlainTextEdit *>("taskResultReport");
                values = rows->toPlainText();
                for (int width : {540, 420}) {
                    dialog->resize(width, 360);
                    QApplication::processEvents();
                    auto *buttons = dialog->findChild<QDialogButtonBox *>("taskResultActions");
                    geometry &= UiInspection::fullyVisible(*rows, *dialog) &&
                                UiInspection::fullyVisible(*buttons, *dialog) &&
                                UiInspection::separate(*rows, *buttons, *dialog);
                    geometry &=
                        UiInspection::writeReport(*dialog, report_ + '-' + QString::number(width));
                }
                dialog->accept();
                return;
            }
            auto *progress = dynamic_cast<CProgressDialog *>(QApplication::activeModalWidget());
            if (!progress)
                return;
            auto *pause = progress->findChild<QPushButton *>("IDB_PAUSE");
            if (pause && !pause->isVisible()) {
                {
                    NWindows::NSynchronization::CCriticalSectionLock lock(progress->Sync._cs);
                    FOR_VECTOR(i, progress->Sync.Messages)
                    errors << ToQString(progress->Sync.Messages[i]);
                }
                progress->findChild<QPushButton *>("IDCANCEL")->click();
            }
        });
        qApp->installEventFilter(this);
        timer_.start(1);
    }
    ~HashModalResponse() override { qApp->removeEventFilter(this); }
    void recovery() {
        cancel = false;
        resultSeen = false;
        geometry = true;
        values.clear();
        errors.clear();
        report_ += "-recovery";
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (cancel && !cancellationRequested && event->type() == QEvent::Show)
            if (auto *progress = dynamic_cast<CProgressDialog *>(object)) {
                // Public source cancellation service, before modal event delivery.
                progress->Sync.Set_Stopped(true);
                cancellationRequested = true;
            }
        return QObject::eventFilter(object, event);
    }
};
} // namespace

class NamespaceHashTests final : public QObject, protected MainWindowTestSupport {
    Q_OBJECT
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static void rootChild(MainWindow &window, unsigned index) {
        action(window, "openRootAction")->trigger();
        action(window, "parentFolderAction")->trigger();
        auto *view = table(window);
        int child = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 6).toUInt() == index)
                child = row;
        QVERIFY(child >= 0);
        view->setCurrentCell(child, 0);
        view->selectRow(child);
        action(window, "openItemAction")->trigger();
        QTRY_VERIFY(view->rowCount() > 0);
    }

  private slots:
    void logicalDriveHashIsAvailableWithoutCopy() {
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
#ifdef Q_OS_WIN
        rootChild(window, 0);
#else
        rootChild(window, 1);
#endif
        table(window)->setCurrentCell(0, 0);
        table(window)->selectRow(0);
#ifdef Q_OS_WIN
        QVERIFY(action(window, "hashSHA256Action")->isEnabled());
#else
        QVERIFY(!action(window, "hashSHA256Action")->isEnabled());
#endif
        QVERIFY(!action(window, "copyAction")->isEnabled());
        QCOMPARE(backend.listCalls, 0);
        // Availability only: never enumerate or hash a host filesystem drive.
    }
#ifdef Q_OS_WIN
    void sourcePrivateDeviceHash_data() {
        QTest::addColumn<QString>("outcome");
        QTest::addColumn<QString>("language");
        for (const char *language : {"-", "ru"})
            for (const char *outcome : {"success", "cancel", "failure"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(language, outcome)))
                    << QString::fromLatin1(outcome) << QString::fromLatin1(language);
    }
    void sourcePrivateDeviceHash() {
        QFETCH(QString, outcome);
        QFETCH(QString, language);
        SaveRegLang(ToUString(language), qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        ReloadLang(qEnvironmentVariable("SEVENZIP_SETTINGS_PATH"));
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        ReadOnlyVirtualDisk disk(temporary.filePath(QStringLiteral("hash диск 空.vhd")));
        QVERIFY2(disk.valid(), qPrintable(QString::number(disk.error())));
        const QByteArray input = disk.readDevice();
        QCOMPARE(quint64(input.size()), ReadOnlyVirtualDisk::Size);
        ControlledBackend backend;
        MainWindow window(&backend);
        window.show();
        rootChild(window, 3);
        auto *view = table(window);
        int selected = -1;
        for (int row = 0; row < view->rowCount(); ++row)
            if (view->model()->index(row, 0).data(Qt::UserRole + 3).toString() ==
                disk.device().mid(4))
                selected = row;
        QVERIFY(selected >= 0);
        view->setCurrentCell(selected, 0);
        view->selectRow(selected);
        auto *hash = action(window, "hashSHA256Action");
        QVERIFY(hash->isEnabled());
        HashModalResponse response("namespace-device-hash-" + language + '-' + outcome);
        response.cancel = outcome == "cancel";
        HANDLE held = INVALID_HANDLE_VALUE;
        if (outcome == "failure") {
            // The attachment stays live and owns this number throughout the
            // operation. Deny another reader only on this fixture's disk.
            const auto native = disk.device().toStdWString();
            held = CreateFileW(native.c_str(), GENERIC_READ, FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
            QVERIFY(held != INVALID_HANDLE_VALUE);
        }
        hash->trigger();
        if (held != INVALID_HANDLE_VALUE)
            CloseHandle(held);
        if (outcome == "success") {
            QVERIFY(response.resultSeen);
            QVERIFY(response.geometry);
            QVERIFY2(response.errors.isEmpty(), qPrintable(response.errors.join('\n')));
            QVERIFY(response.values.contains(QString::fromLatin1(
                QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex())));
        } else if (outcome == "cancel") {
            QVERIFY(response.cancellationRequested);
            QVERIFY(!response.resultSeen);
            QVERIFY2(response.errors.isEmpty(), qPrintable(response.errors.join('\n')));
        } else {
            QVERIFY(!response.errors.isEmpty());
        }
        if (outcome != "success") {
            response.recovery();
            hash->trigger();
            QVERIFY(response.resultSeen);
            QVERIFY(response.geometry);
            QVERIFY2(response.errors.isEmpty(), qPrintable(response.errors.join('\n')));
            QVERIFY(response.values.contains(QString::fromLatin1(
                QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex())));
        }
        QCOMPARE(disk.readDevice(), input);
        QCOMPARE(backend.listCalls, 0);
        QCOMPARE(view->selectionModel()->selectedRows().size(), 1);
        QVERIFY(hash->isEnabled());
        QCOMPARE(disk.detach(), DWORD(ERROR_SUCCESS));
    }
#endif
};
QTEST_MAIN(NamespaceHashTests)
#include "namespace_hash_tests.moc"
