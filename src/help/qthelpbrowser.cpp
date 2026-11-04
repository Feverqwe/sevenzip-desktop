#include "help/qthelpbrowser_p.h"
#include "ui/theme/qtsemanticcolors.h"
#include <QEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>

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
} // namespace

HelpBrowser::HelpBrowser(QWidget *parent, const QUrl &root) : QTextBrowser(parent) {
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

QVariant HelpBrowser::loadResource(int type, const QUrl &url) {
    const QVariant resource = QTextBrowser::loadResource(type, url);
    if (type != QTextDocument::StyleSheetResource || url.fileName() != "style.css" ||
        !resource.isValid())
        return resource;
    const QByteArray css = resource.toByteArray();
    styles.insert(url, css);
    return themedStyle(css);
}

void HelpBrowser::changeEvent(QEvent *event) {
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        refreshTheme();
}

bool HelpBrowser::eventFilter(QObject *object, QEvent *event) {
    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate)
        refreshTheme();
    return QTextBrowser::eventFilter(object, event);
}

void HelpBrowser::refreshTheme() {
    if (source().isEmpty())
        return;
    bool changed = false;
    for (auto it = styles.cbegin(); it != styles.cend(); ++it) {
        const QByteArray css = themedStyle(it.value());
        if (document()->resource(QTextDocument::StyleSheetResource, it.key()).toByteArray() == css)
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

void HelpBrowser::resolveHelpLinks(const QUrl &root, const QUrl &source) {
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

QByteArray HelpBrowser::themedStyle(QByteArray css) const {
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
