#include "ui/reports/qttextreport.h"
#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QPlainTextEdit>

void QtConfigureTextReport(QPlainTextEdit &edit, const QFont &baseFont) {
    QFont reportFont = baseFont;
    reportFont.setFamilies(QFontDatabase::systemFont(QFontDatabase::FixedFont).families());
    reportFont.setStyleHint(QFont::Monospace);
    edit.setFont(reportFont);
    edit.setReadOnly(true);
    edit.setLineWrapMode(QPlainTextEdit::NoWrap);
    edit.setTabChangesFocus(true);
}

void QtCopyTextReport(QPlainTextEdit &edit) {
    if (edit.textCursor().hasSelection())
        edit.copy();
    else
        QtCopyFullTextReport(edit);
}

void QtCopyFullTextReport(const QPlainTextEdit &edit) { QtCopyFullTextReport(edit.toPlainText()); }

void QtCopyFullTextReport(const QString &text) { QApplication::clipboard()->setText(text); }
