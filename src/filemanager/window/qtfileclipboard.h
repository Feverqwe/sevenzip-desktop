#pragma once
#include <QCoreApplication>
#include <QObject>
#include <QPointer>
#include <QSet>

class MainWindow;
class QAction;
class QMimeData;

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
    Availability AvailabilityForPanel(unsigned panel);
    QAction *cut;
    QAction *copy;
    QAction *paste;

  private:
    void UpdateClipboardSnapshot();
    MainWindow &window_;
    QSet<QString> cutPaths_;
    QPointer<const QMimeData> clipboardMime_;
    quint64 revision_ = 0;
    bool clipboardDirty_ = true;
    bool hasClipboardFiles_ = false;
};
