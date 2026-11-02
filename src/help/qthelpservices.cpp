#include "help/qthelpservices.h"
#include "localization/qtlangbuttonbox.h"
#include "localization/qtlangcontrols.h"
#include "ui/dialogs/qtdialoglayout.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextTable>
#include <QUrl>
#include <QVBoxLayout>

static void InitHelpResources() { Q_INIT_RESOURCE(upstream_help); }

namespace {
void FitHelpTableColumns(QTextFrame *frame) {
    const auto keepCellLines = [](const QTextTableCell &cell) {
        auto cursor = cell.firstCursorPosition();
        cursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
        QTextBlockFormat block;
        block.setNonBreakableLines(true);
        cursor.mergeBlockFormat(block);
    };
    for (auto *child : frame->childFrames()) {
        if (auto *table = qobject_cast<QTextTable *>(child)) {
            auto format = table->format();
            auto widths = format.columnWidthConstraints();
            bool changed = false;
            for (qsizetype column = 0; column < widths.size(); ++column) {
                auto &width = widths[column];
                if (width.type() == QTextLength::FixedLength) {
                    width = QTextLength(QTextLength::VariableLength, 0);
                    changed = true;
                    for (int row = 0; row < table->rows(); ++row) {
                        const auto cell = table->cellAt(row, int(column));
                        if (cell.columnSpan() != 1)
                            continue;
                        keepCellLines(cell);
                    }
                }
            }
            if (changed) {
                // These source widths are declared on header cells. Preserve
                // readable header words when the table scrolls in a narrow view.
                for (int column = 0; column < table->columns(); ++column)
                    keepCellLines(table->cellAt(0, column));
                format.setColumnWidthConstraints(widths);
                table->setFormat(format);
            }
        }
        FitHelpTableColumns(child);
    }
}

// Qt presentation adapter for the original CHM pages, with palette colors and
// a small reading stylesheet for tables and command blocks.
class HelpBrowser final : public QTextBrowser {
  public:
    HelpBrowser(QWidget *parent, const QUrl &root) : QTextBrowser(parent) {
        setBackgroundRole(QPalette::Window);
        viewport()->setBackgroundRole(QPalette::Window);
        window()->installEventFilter(this);
        connect(this, &QTextBrowser::sourceChanged, this, [this, root](const QUrl &url) {
            // source() can still name the previous history entry in this signal.
            resolveHelpLinks(root, url);
            // Legacy pixel widths can be smaller than a parameter name with
            // the current font/padding. Let Qt size those columns by content.
            FitHelpTableColumns(document()->rootFrame());
        });
    }

  protected:
    QVariant loadResource(int type, const QUrl &url) override {
        const QVariant resource = QTextBrowser::loadResource(type, url);
        if (type != QTextDocument::StyleSheetResource || url.fileName() != "style.css" ||
            !resource.isValid())
            return resource;
        const QByteArray css = resource.toByteArray();
        styles.insert(url, css);
        return themedStyle(css);
    }

    void changeEvent(QEvent *event) override {
        QTextBrowser::changeEvent(event);
        if (event->type() == QEvent::PaletteChange)
            refreshTheme();
    }

    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate)
            refreshTheme();
        return QTextBrowser::eventFilter(object, event);
    }

  private:
    void refreshTheme() {
        if (source().isEmpty())
            return;
        bool changed = false;
        for (auto it = styles.cbegin(); it != styles.cend(); ++it) {
            const QByteArray css = themedStyle(it.value());
            if (document()->resource(QTextDocument::StyleSheetResource, it.key()).toByteArray() ==
                css)
                continue;
            document()->addResource(QTextDocument::StyleSheetResource, it.key(), css);
            changed = true;
        }
        if (!changed)
            return;
        const int scroll = verticalScrollBar()->value();
        const int horizontalScroll = horizontalScrollBar()->value();
        reload();
        verticalScrollBar()->setValue(scroll);
        horizontalScrollBar()->setValue(horizontalScroll);
    }

    void resolveHelpLinks(const QUrl &root, const QUrl &source) {
        // QTextBrowser resolves hrefs before emitting anchorClicked, so resolve
        // them inside the CHM namespace before mouse/keyboard activation.
        QUrl member;
        member.setScheme("help");
        member.setPath('/' + source.path().mid(root.path().size()));
        struct Link {
            int start;
            int length;
            QString href;
        };
        QList<Link> links;
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment();
                const auto format = fragment.charFormat();
                const QUrl href(format.anchorHref());
                if (!format.isAnchor() || href.isEmpty() || !href.isRelative() ||
                    !href.authority().isEmpty())
                    continue;
                const QUrl resolved = member.resolved(href);
                QUrl target = root;
                target.setPath(root.path() + resolved.path().mid(1).toLower());
                target.setFragment(resolved.fragment());
                target.setQuery(resolved.query());
                links.append({fragment.position(), fragment.length(), target.toString()});
            }
        }
        for (const auto &link : links) {
            QTextCursor cursor(document());
            cursor.setPosition(link.start);
            cursor.setPosition(link.start + link.length, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setAnchorHref(link.href);
            cursor.mergeCharFormat(format);
        }
    }

    QByteArray themedStyle(QByteArray css) const {
        // Append to the original stylesheet so upstream selectors/classes,
        // semantic emphasis and alignments remain available.
        css += R"css(
table {
    border-collapse: collapse;
    border: 1px solid #999999;
    background: #ffffff;
    margin-top: 12px;
    margin-bottom: 16px;
}
th, td {
    border: 1px solid #999999;
    padding: 7px 10px;
    font-size: 100%;
}
th { background: #cccccc; }
pre, pre *, code {
    font-family: "Menlo", "Consolas", "DejaVu Sans Mono", monospace;
    font-size: medium;
}
pre, pre.syntax, pre.example {
    background: #dddddd;
    margin-top: 0px;
    margin-bottom: 0px;
    margin-left: 10px;
    margin-right: 10px;
    white-space: pre;
}
)css";
        const QPalette colors = palette();
        const QColor base = colors.color(QPalette::Window);
        const QColor text = colors.color(QPalette::WindowText);
        const QColor surface = QtSemanticColors::surface(colors, QPalette::Window);
        const QColor link = QtSemanticColors::readable(colors.color(QPalette::Link), base);
        // Substitute in one pass: palette colors may equal another source token.
        const QHash<QByteArray, QColor> replacements = {
            {"#ffffff", base},
            {"#000000", text},
            {"#999999", QtSemanticColors::border(colors, QPalette::Window)},
            {"#cccccc", surface},
            {"#dddddd", surface},
            {"#0066ff", QtSemanticColors::readable(link, surface)},
            {"#996600", QtSemanticColors::readable(colors.color(QPalette::LinkVisited), base)},
            {"#cc9900", QtSemanticColors::readable(link, surface)},
            {"#C00000", QtSemanticColors::error(surface)},
            {"#F00000", QtSemanticColors::error(surface)},
            {"#228B22", QtSemanticColors::success(base)}};
        QByteArray result;
        for (qsizetype i = 0; i < css.size();) {
            const auto color = replacements.constFind(css.mid(i, 7));
            if (css[i] == '#' && color != replacements.cend()) {
                result += color.value().name().toLatin1();
                i += 7;
            } else {
                result += css[i++];
            }
        }
        return result;
    }

    QHash<QUrl, QByteArray> styles;
};

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
