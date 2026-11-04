#pragma once
#include <QCoreApplication>
#include <QObject>

class MainWindow;
class QAction;

class QtFileClipboard final : public QObject {
    Q_DECLARE_TR_FUNCTIONS(QtFileClipboard)
  public:
    explicit QtFileClipboard(MainWindow &window);
    void Copy(bool cut);
    void Paste();
    void Refresh();
    struct Availability {
        bool cut = false, copy = false, paste = false;
    };
    Availability AvailabilityForPanel(unsigned panel) const;
    QAction *cut;
    QAction *copy;
    QAction *paste;

  private:
    MainWindow &window_;
};
