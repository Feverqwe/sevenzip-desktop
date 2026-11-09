#pragma once

#include <QString>

inline QString QtSingleLineCaption(QString text) {
    return text.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
}

// Dynamic values must stay in the caption, outside Qt's mnemonic and shortcut syntax.
inline QString QtLiteralMenuText(const QString &text) {
    return QtSingleLineCaption(text).replace('&', "&&");
}
