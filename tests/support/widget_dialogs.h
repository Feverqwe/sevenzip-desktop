#pragma once

#include <QDialog>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

// Public-Qt responders and widget geometry inspect Qt dialogs. Native OS panels
// require their separate platform/manual acceptance rather than these fixtures.
class WidgetDialogFixture final : public QObject {
    const bool previous_;
    struct Shown {
        int loop;
        quint64 generation;
        bool settled = false;
    };
    QHash<const QDialog *, Shown> shown_;
    quint64 generation_ = 0;
    QSet<const QDialog *> answering_;
    bool eventFilter(QObject *watched, QEvent *event) override;

  public:
    WidgetDialogFixture();
    ~WidgetDialogFixture() override;
    bool ready(QDialog &dialog) const;
    QDialog *activeDialog() const;
    void respond(QObject &context, std::function<void(QDialog &)> callback);
    void connectResponder(QTimer &timer, QObject *context, std::function<void()> callback);
    WidgetDialogFixture(const WidgetDialogFixture &) = delete;
    WidgetDialogFixture &operator=(const WidgetDialogFixture &) = delete;
};
