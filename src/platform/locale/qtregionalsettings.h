#pragma once
#include <QDateTime>
#include <QLocale>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

class QtRegionalNotification {
  public:
    virtual ~QtRegionalNotification() = default;
};
std::unique_ptr<QtRegionalNotification> QtCreateRegionalNotification(std::function<void()> changed);
QLocale QtSystemRegionalUnitsLocale();
QString QtSystemRegionalNumber(quint64 value);
QString QtSystemRegionalNumber(double value, int precision);
QString QtSystemRegionalDateTime(const QDateTime &local, QLocale::FormatType format);

// Presentation subscribers refresh without reloading their source or changing
// the application's translation language. Native notifications are coalesced
// and delivered on the application thread, including Windows top-level forms.
class QtRegionalSettings final : public QObject {
    Q_OBJECT
  public:
    static void Observe(QObject &receiver, std::function<void()> refresh);

  signals:
    void Changed();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    explicit QtRegionalSettings(QObject &application);
    void QueueRefresh();
    QTimer pending_;
    std::unique_ptr<QtRegionalNotification> native_;
};
