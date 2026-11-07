#pragma once

#include <QString>

// Dynamic values must stay in the caption, outside Qt's mnemonic and shortcut syntax.
inline QString QtLiteralMenuText(QString text) {
    return text.replace('&', "&&").replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
}
