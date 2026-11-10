#pragma once

#include <QString>
#include <QTextDocument>

inline QString QtSingleLineCaption(QString text) {
    return text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
}

// Dynamic values must stay in the caption, outside Qt's mnemonic and shortcut syntax.
inline QString QtLiteralMenuText(const QString &text) {
    return QtSingleLineCaption(text).replace('&', "&&");
}

inline QString QtLiteralToolTip(const QString &text) {
    return Qt::mightBeRichText(text) ? Qt::convertFromPlainText(text, Qt::WhiteSpacePre) : text;
}
