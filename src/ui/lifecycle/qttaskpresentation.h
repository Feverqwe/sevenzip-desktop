#pragma once
#include "operations/qtoperationprotocol.h"
#include <QObject>
#include <QPointer>
#include <QVector>

class QWidget;

// Presentation of the existing native job in this process. It owns no worker,
// archive input or cancellation policy.
class QtTaskPresentation final : public QObject {
    Q_OBJECT
  public:
    class QuestionScope final {
        QPointer<QtTaskPresentation> owner_;

      public:
        QuestionScope();
        ~QuestionScope();
        Q_DISABLE_COPY_MOVE(QuestionScope)
    };
    using Phase = QtOperationPhase;
    struct Snapshot {
        Phase phase = Phase::Preparing;
        QString title;
        bool hasWindow = false;
        bool operator==(const Snapshot &) const = default;
    };
    static QtTaskPresentation &Application();
    static void Watch(QWidget &window, Phase phase);
    static void SetPhase(QWidget &window, Phase phase);
    static void BeginWork(QWidget &window);
    Snapshot snapshot() const;
    bool workStarted() const { return workStarted_; }
    bool resultReviewed() const { return resultReviewed_; }
    static void ResultReviewed();
    static bool CanPresent(const QWidget *owner = nullptr);
    void Show();
  signals:
    void changed();

  private:
    explicit QtTaskPresentation(QObject *parent);
    struct Entry {
        QPointer<QWidget> window;
        Phase phase;
        quint64 order = 0;
    };
    QWidget *currentWindow() const;
    const Entry *currentEntry() const;
    bool eventFilter(QObject *object, QEvent *event) override;
    void Publish();
    QVector<Entry> entries_;
    quint64 sequence_ = 0;
    Phase fallback_ = Phase::Preparing;
    Snapshot published_;
    bool showPending_ = false;
    bool workStarted_ = false;
    bool resultReviewed_ = false;
    unsigned questionDepth_ = 0;
};
