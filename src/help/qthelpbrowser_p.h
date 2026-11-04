// Private document adapter for the bundled 7-Zip help pages.
#pragma once
#include <QByteArray>
#include <QHash>
#include <QTextBrowser>
#include <QUrl>

class HelpBrowser final : public QTextBrowser {
  public:
    HelpBrowser(QWidget *parent, const QUrl &root);

  protected:
    QVariant loadResource(int type, const QUrl &url) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void refreshTheme();
    void resolveHelpLinks(const QUrl &root, const QUrl &source);
    QByteArray themedStyle(QByteArray css) const;
    QHash<QUrl, QByteArray> styles;
};
