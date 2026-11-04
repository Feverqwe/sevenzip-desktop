#include "help/qthelpservices.h"
#include "help/qthelpbrowser_p.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

static void InitHelpResources() { Q_INIT_RESOURCE(upstream_help); }

namespace {
HelpBrowser *CreateHelpBrowser(QWidget *parent, const QUrl &root, const QString &title = {}) {
    auto *window = new QDialog(parent);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setObjectName("helpWindow");
    if (title.isEmpty())
        QtBindLocalText(window, "qthelpservices",
                        QT_TRANSLATE_NOOP("qthelpservices", "7-Zip Help"));
    else
        window->setWindowTitle(title);
    window->resize(760, 560);
    auto *buttons = new QtLangButtonBox(QDialogButtonBox::Close, window);
    buttons->setObjectName("helpButtons");
    auto *layout = QtCreateDialogLayout(*window, *buttons);
    auto *browser = new HelpBrowser(window, root);
    browser->setObjectName("helpBrowser");
    browser->setOpenLinks(false);
    layout->addWidget(browser, 1);
    QObject::connect(browser, &QTextBrowser::anchorClicked, window, [browser](const QUrl &url) {
        const QUrl target = browser->source().resolved(url);
        if (target.scheme() == "http" || target.scheme() == "https")
            QDesktopServices::openUrl(target);
        else if (target.scheme() == browser->source().scheme())
            browser->setSource(target);
    });
    auto *back = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    back->setObjectName("helpBack");
    QtBindLocalText(back, "qthelpservices", QT_TRANSLATE_NOOP("qthelpservices", "Back"));
    back->setEnabled(false);
    QObject::connect(browser, &QTextBrowser::backwardAvailable, back, &QPushButton::setEnabled);
    QObject::connect(back, &QPushButton::clicked, browser, &QTextBrowser::backward);
    QObject::connect(buttons, &QDialogButtonBox::rejected, window, &QDialog::close);
    return browser;
}
} // namespace

void QtOpenHelpTopic(const QString &topic, QWidget *parent, const QString &root) {
    InitHelpResources();
    const QUrl relative(topic);
    QString fileName = relative.path().toLower(); // Native CHM paths are case insensitive.
    if (fileName.isEmpty())
        fileName = "start.htm";
    const QString base = root.isEmpty() ? ":/upstream/7zip/Help" : root;
    const QString path = base + '/' + fileName;
    if (!relative.isRelative() || fileName.startsWith('/') || fileName.contains("..") ||
        !QFile::exists(path)) {
        QMessageBox::warning(
            parent, QCoreApplication::translate("qthelpservices", "7-Zip Help"),
            QCoreApplication::translate("qthelpservices", "Help topic is unavailable: %1")
                .arg(topic));
        return;
    }
    const QUrl helpRoot = root.isEmpty() ? QUrl("qrc:/upstream/7zip/Help/")
                                         : QUrl::fromLocalFile(QDir(root).absolutePath() + '/');
    auto *browser = CreateHelpBrowser(parent, helpRoot);
    QUrl source =
        root.isEmpty() ? QUrl("qrc:/upstream/7zip/Help/" + fileName) : QUrl::fromLocalFile(path);
    source.setFragment(relative.fragment());
    browser->setSource(source);
    browser->window()->show();
    browser->setFocus(Qt::OtherFocusReason);
}

void QtOpenHelpText(const QString &title, const QString &text, QWidget *parent) {
    auto *browser = CreateHelpBrowser(parent, {}, title);
    browser->setPlainText(text);
    browser->window()->show();
    browser->setFocus(Qt::OtherFocusReason);
}
