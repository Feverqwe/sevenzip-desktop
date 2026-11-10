#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUuid>
#include <functional>
#include <vector>

class MainWindow;
class CPanel;
class QApplication;
class QtTaskResultDialog;
class QWidget;
struct QtArchiveLocation;

// One application owner writes the complete window session, so closing one
// window cannot overwrite its neighbours' tabs with legacy panel preferences.
class QtWorkspaceApplication final : public QObject {
  public:
    explicit QtWorkspaceApplication(QApplication &application);
    ~QtWorkspaceApplication() override;
    static QtWorkspaceApplication *Find();
    void Register(MainWindow &window);
    void Closing(MainWindow &window);
    void RestoreInitialGeometry(MainWindow &first);
    void Restore(MainWindow &first);
    void NewWindowFrom(MainWindow &source);
    void Save();
    void ArchivePublished(const QString &archive, const CPanel *updated = nullptr);
    void FilesystemPublished(const QStringList &directories, const MainWindow *exclude = nullptr);
    void FileListSettingsChanged(const MainWindow &source);
    void Quit();

  private:
    void OpenWindow(const QJsonObject &state, QtArchiveLocation archive);
    void ApplyWhenReady(MainWindow &window, const QJsonObject &state,
                        const QtArchiveLocation &archive);
    std::vector<QPointer<MainWindow>> windows_;
    QJsonArray ReadSession() const;
    bool restoring_ = false;
    bool quitting_ = false;
    unsigned pendingRestores_ = 0;
    void InitializeTaskResults();
    void ShowTaskReview(const QUuid &id, bool activate);
    bool eventFilter(QObject *object, QEvent *event) override;
    void PresentTaskReviews();
    QPointer<QWidget> reviewModal_;
    QHash<QUuid, QPointer<QtTaskResultDialog>> taskResults_;
};
