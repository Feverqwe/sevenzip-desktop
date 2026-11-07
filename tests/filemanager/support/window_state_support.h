#pragma once
#include "filemanager/views/filetable.h"

#include "filemanager/window/mainwindow.h"
#include "operations/qtoperationlauncher.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedPointer>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QSysInfo>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QtTest>

class ControlledBackend : public ArchiveBackend {
  public:
    bool discoverCodecCatalog(const QString & = {}) override { return false; }
    ControlledBackend() : ArchiveBackend(nullptr, "/missing/7z") {
        connect(this, &ArchiveBackend::infoReady, this,
                [this](const ArchiveInfo &) { listedInfoReported_ = true; });
    }
    ~ControlledBackend() override {
        for (const QString &path : ownedArchiveFixtures_)
            QFile::remove(path);
    }
    bool prepareArchiveFixture(const QString &path) {
        if (QFileInfo(path).isFile() && !QFileInfo(path).isSymLink())
            return true;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        file.close();
        ownedArchiveFixtures_.append(QFileInfo(path).absoluteFilePath());
        return true;
    }
    void queueList(const QList<ArchiveEntry> &entries) {
        nextListCompletion_ = [this, entries] { completeList(entries); };
    }
    void queueListAction(std::function<void()> action) { nextListCompletion_ = std::move(action); }
    void queueSourceExtractAction(std::function<void()> action) {
        nextSourceExtractCompletion_ = std::move(action);
    }
    void extractSourceEntry(const QString &archive, const QString &entry, const QString &output,
                            const QString &, const QString &password, bool = false) override {
        extract(archive, output, {entry}, password);
        if (nextSourceExtractCompletion_) {
            auto completion = std::move(nextSourceExtractCompletion_);
            nextSourceExtractCompletion_ = {};
            QTimer::singleShot(0, this, std::move(completion));
        } else {
            QTimer::singleShot(0, this, [this] { cancel(); });
        }
    }
    bool busy() const override { return busy_; }
    HRESULT lastResult() const override { return result_; }
    void openFolder(const QString &archive, const QString &format,
                    const QString &password = {}) override {
        lastOpenFormat = format;
        list(archive, password);
    }
    void list(const QString &archive, const QString & = {}) override {
        listedInfoReported_ = false;
        ++listCalls;
        lastArchive = archive;
        busy_ = true;
        if (nextListCompletion_) {
            auto completion = std::move(nextListCompletion_);
            nextListCompletion_ = {};
            QTimer::singleShot(0, this, std::move(completion));
        }
    }
    void extract(const QString &, const QString &destination, const QStringList &selected,
                 const QString &password = {}, const ExtractionOptions &options = {}) override {
        ++extractCalls;
        lastSelected = selected;
        lastExtractDestination = destination;
        lastExtractPassword = password;
        lastExtractionOptions = options;
        busy_ = true;
    }
    void test(const QString &, const QString &password = {}) override {
        ++testCalls;
        lastTestPassword = password;
        busy_ = true;
    }
    // Synthetic providers advertise their own public capabilities. Native
    // writer behavior is exercised with real archives in the component suites.
    bool supportsArchiveUpdate(const QString &format) const override {
        return format.compare("7z", Qt::CaseInsensitive) == 0 ||
               format.compare("zip", Qt::CaseInsensitive) == 0;
    }
    QtOperationLauncher *startGuiCompress(const QString &archive, const QString &type,
                                          bool addExtension, const QStringList &paths, bool email,
                                          bool showDialog, QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Compress;
        job.ArchivePath = archive;
        job.ArchiveType = type;
        job.AddExtension = addExtension;
        job.Paths = paths;
        job.EMail = email;
        job.ShowDialog = showDialog;
        return captureGuiRequest(std::move(job), error);
    }
    QtOperationLauncher *startGuiHash(const QStringList &paths, const QString &method,
                                      const QString &basePrefix, bool enterToDirs,
                                      QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Hash;
        job.Paths = paths;
        job.HashMethod = method;
        job.HashBasePrefix = basePrefix;
        job.HashEnterToDirs = enterToDirs;
        return captureGuiRequest(std::move(job), error);
    }
    QtOperationLauncher *startGuiTest(const QStringList &paths, QString &error,
                                      bool hashMode = false) override {
        QtOperationJob job;
        job.Paths = paths;
        job.HashMode = hashMode;
        return captureGuiRequest(std::move(job), error);
    }
    QtOperationLauncher *startGuiExtract(const QStringList &paths, const QString &destination,
                                         bool showDialog, bool elimDup, quint32 writeZone,
                                         QString &error) override {
        QtOperationJob job;
        job.Operation = QtOperationJob::Command::Extract;
        job.Paths = paths;
        job.Destination = destination;
        job.ShowDialog = showDialog;
        job.ElimDup = elimDup;
        job.WriteZone = writeZone;
        return captureGuiRequest(std::move(job), error);
    }
    QList<QtOperationJob> GuiRequests;
    void cancel() override {
        ++cancelCalls;
        busy_ = false;
        result_ = E_ABORT;
        emit finished(false, QStringLiteral("Операция отменена."));
    }
    HRESULT setPaused(bool) override { return S_OK; }
    void completeList(const QList<ArchiveEntry> &entries) {
        if (!listedInfoReported_) {
            // Model the engine's typed metadata for this disposable fixture.
            // Explicit infoReady responses (encryption/offset/format) win.
            ArchiveInfo info;
            info.format = QFileInfo(lastArchive).suffix().toLower();
            emit infoReady(info);
        }
        busy_ = false;
        result_ = S_OK;
        emit entriesReady(entries);
        emit finished(true, QStringLiteral("Архив открыт."));
    }
    void fail(const QString &message, HRESULT result = E_FAIL) {
        busy_ = false;
        result_ = result;
        emit finished(false, message);
    }
    void completeExtraction() {
        busy_ = false;
        emit finished(true, QStringLiteral("Извлечение завершено."));
    }
    void report(int percent, const QString &detail, const QString &line) {
        emit outputReceived(line);
        emit progress(percent, detail);
    }
    int listCalls = 0;
    int extractCalls = 0;
    int testCalls = 0;
    bool listedInfoReported_ = false;
    QString lastOpenFormat;
    int cancelCalls = 0;
    QString lastArchive;
    QStringList lastSelected;
    QString lastExtractDestination;
    QString lastExtractPassword;
    ExtractionOptions lastExtractionOptions;
    QString lastTestPassword;

  private:
    QtOperationLauncher *captureGuiRequest(QtOperationJob job, QString &error) {
        GuiRequests.append(std::move(job));
        error.clear();
        auto *operation = new QtOperationLauncher(this);
        QTimer::singleShot(0, operation, [operation] {
            emit operation->accepted();
            emit operation->finished({QtOperationResult::Outcome::Cancelled, 255, {}});
            operation->deleteLater();
        });
        return operation;
    }
    bool busy_ = false;
    HRESULT result_ = S_OK;
    QStringList ownedArchiveFixtures_;
    std::function<void()> nextListCompletion_;
    std::function<void()> nextSourceExtractCompletion_;
};

class MainWindowTestSupport {
  protected:
    static void openListedArchive(MainWindow &window, ControlledBackend &backend,
                                  const QString &path, const QList<ArchiveEntry> &entries) {
        QVERIFY(backend.prepareArchiveFixture(path));
        backend.queueList(entries);
        window.openArchive(path);
    }
    static QAction *action(MainWindow &window, const char *name) {
        auto *result = window.findChild<QAction *>(name);
        Q_ASSERT(result);
        return result;
    }
    static FileTable *table(MainWindow &window) {
        auto *result = window.findChild<FileTable *>("archiveTable");
        Q_ASSERT(result);
        return result;
    }
    static int rowWithPath(MainWindow &window, const QString &path) {
        for (int row = 0; row < table(window)->rowCount(); ++row) {
            if (table(window)->model()->index(row, 0).data(Qt::UserRole).toString() == path)
                return row;
        }
        return -1;
    }
    static void openRow(MainWindow &window, int row) {
        QVERIFY(row >= 0);
        QVERIFY(
            QMetaObject::invokeMethod(table(window), "doubleClicked", Qt::DirectConnection,
                                      Q_ARG(QModelIndex, table(window)->model()->index(row, 0))));
    }
    static void dismissWarnings() {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *box = qobject_cast<QMessageBox *>(widget))
                box->accept();
        }
    }
    static void pressSelectAll(FileTable *view) {
        const QKeyCombination key = QKeySequence(QKeySequence::SelectAll)[0];
        QTest::keyClick(view, key.key(), key.keyboardModifiers());
    }
    static int filesystemRow(MainWindow &window, const QString &name) {
        for (int row = 0; row < table(window)->rowCount(); ++row) {
            if (table(window)->model()->index(row, 0).data().toString() == name)
                return row;
        }
        return -1;
    }

  protected:
};
