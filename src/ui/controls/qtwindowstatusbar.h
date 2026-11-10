#pragma once
#include <QDeadlineTimer>
#include <QHash>
#include <QPointer>
#include <QStatusBar>

class QLabel;

// One window strip. The window supplies source identity and current summaries;
// this control owns only message expiry and presentation.
class QtWindowStatusBar final : public QStatusBar {
  public:
    explicit QtWindowStatusBar(QWidget *parent = nullptr);
    void SetSummary(QObject *context, const QString &summary, const QString &size,
                    const QString &exactSize, bool loading);
    void SetMessage(QObject *context, const QString &message, int timeout = 5000);
    void ClearMessageIf(QObject *context, const QString &message);

  private:
    struct Feedback {
        QString text;
        QDeadlineTimer expiry;
    };
    QHash<QObject *, Feedback> feedback_;
    QPointer<QObject> context_;
    QLabel *summary_, *size_;
    bool loading_ = false;
    bool rendering_ = false;
    void WatchContext(QObject *context);
    void Render();
};
