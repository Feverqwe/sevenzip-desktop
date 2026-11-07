#pragma once
#include "mount/qtmountrow.h"
#include <QCoreApplication>
#include <QDialog>
#include <QList>
#include <QPointer>
#include <QStringList>

class QtArchiveMountSession;
struct IUnknown;
class QLabel;
class QVBoxLayout;
class QScrollArea;

// Modeless mount management for a file pane.
class QtArchiveMountManager final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(QtArchiveMountManager)
    QWidget *owner_;
    struct Session {
        QtArchiveMountSession *instance;
        QStringList issues;
        QtMountRow *row = nullptr;
        bool folder = false;
    };
    QList<Session> sessions_;
    QVBoxLayout *entries_;
    QScrollArea *scroll_;
    QLabel *empty_;
    QPointer<QtMountRow> pendingFocus_;
    bool closingOwner_ = false;
    bool firstShow_ = true;
    void bindSession(QtArchiveMountSession &session);
    void refresh();
    QtMountRowData rowData(const Session &entry) const;
    void requestAction(QtArchiveMountSession &session, QtMountRow::Action action);
    void unmountSession(QtArchiveMountSession &session, bool force);
    void removeSession(QtArchiveMountSession &session);
    void showManager();
    void focusPendingEntry();
    void retranslate();

  protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

  public:
    explicit QtArchiveMountManager(QWidget *owner);
    void mount(IUnknown *resource, const QString &label, const QString &archivePassword,
               const QString &folder = {});
    bool canCloseOwner();
    bool hasActiveSessions() const;
};
