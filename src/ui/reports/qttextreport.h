#pragma once

class QFont;
class QPlainTextEdit;
class QString;

void QtConfigureTextReport(QPlainTextEdit &edit, const QFont &baseFont);
void QtCopyTextReport(QPlainTextEdit &edit);
void QtCopyFullTextReport(const QPlainTextEdit &edit);
void QtCopyFullTextReport(const QString &text);
