#include "filemanager/panels/qtnativepanelkeys.h"

#include <QKeyEvent>
#include <QtGui/private/qkeymapper_p.h>
#include <type_traits>
#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif

namespace {
template <typename T> QKeyCombination keyCombination(const T &candidate) {
    if constexpr (std::is_integral_v<T>)
        return QKeyCombination::fromCombined(candidate);
    else
        return candidate;
}
#ifdef Q_OS_MACOS
QKeyCombination zeroVirtualCommandKey(const QKeyEvent &key) {
    // QAppleKeyMapper::possibleKeyCombinations rejects virtual key zero,
    // which is a valid Cocoa key. Resolve its Command layer using the same
    // system keyboard-layout service, without a physical-letter mapping.
    if (key.nativeVirtualKey() || key.nativeScanCode() || !key.nativeModifiers() ||
        !key.modifiers().testFlag(Qt::ControlModifier))
        return {};
    const auto source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source)
        return {};
    const auto data =
        static_cast<CFDataRef>(TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData));
    UInt32 deadState = 0;
    UniChar text[4];
    UniCharCount length = 0;
    const auto result =
        data ? UCKeyTranslate(reinterpret_cast<const UCKeyboardLayout *>(CFDataGetBytePtr(data)), 0,
                              kUCKeyActionDown, cmdKey >> 8, LMGetKbdType(),
                              kUCKeyTranslateNoDeadKeysMask, &deadState, 4, &length, text)
             : paramErr;
    CFRelease(source);
    return result == noErr && length == 1
               ? QKeyCombination(key.modifiers(), Qt::Key(QChar(text[0]).toUpper().unicode()))
               : QKeyCombination();
}
#endif
} // namespace

bool QtPanelShortcutMatches(const QKeyEvent &key, const QList<QKeySequence> &shortcuts) {
    if (key.modifiers().testFlag(Qt::KeypadModifier) && key.key() >= Qt::Key_0 &&
        key.key() <= Qt::Key_9)
        return false;
    const auto matches = [&](QKeyCombination candidate) {
        return shortcuts.contains(QKeySequence(candidate));
    };
    const auto combination =
        key.key() == Qt::Key_Enter
            ? QKeyCombination(key.modifiers() & ~Qt::KeypadModifier, Qt::Key_Enter)
            : key.keyCombination();
    if (matches(combination))
        return true;
    // Windows virtual digits remain stable when Shift changes the logical
    // symbol. Use the same decoding as the source bookmark dispatcher so the
    // new Shift-2 chord takes priority on that path too.
    if (matches(QKeyCombination(key.modifiers(), Qt::Key(QtPanelVirtualKey(key)))))
        return true;
    // Cocoa's A virtual key is zero; native modifier flags distinguish that
    // real notification from QtTest's entirely synthetic key events.
    if (key.nativeScanCode() || key.nativeVirtualKey() || key.nativeModifiers())
        for (const auto &candidate : QKeyMapper::possibleKeys(&key))
            if (matches(keyCombination(candidate)))
                return true;
    return false;
}

int QtPanelVirtualKey(const QKeyEvent &key) {
    const int logical = key.key();
    if (logical == Qt::Key_Backtab)
        return Qt::Key_Tab;
    if (key.modifiers().testFlag(Qt::KeypadModifier))
        return logical >= Qt::Key_0 && logical <= Qt::Key_9 ? Qt::Key_unknown : logical;
#ifdef Q_OS_WIN
    // NMLVKEYDOWN retains virtual digits when Shift translates them to symbols.
    const auto native = key.nativeVirtualKey();
    if (native >= '0' && native <= '9')
        return static_cast<int>(native);
#endif
    if (!(key.modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ||
        (!key.nativeScanCode() && !key.nativeVirtualKey() && !key.nativeModifiers()))
        return logical;
    // QPA already resolves native keys using the current keyboard layout.
    for (const auto &candidate : QKeyMapper::possibleKeys(&key)) {
        const int native = keyCombination(candidate).key();
        if ((native >= Qt::Key_0 && native <= Qt::Key_9) ||
            (native >= Qt::Key_A && native <= Qt::Key_Z))
            return native;
    }
#ifdef Q_OS_MACOS
    const auto commandKey = zeroVirtualCommandKey(key);
    if (commandKey.key() != Qt::Key_unknown)
        return commandKey.key();
#endif
    return logical;
}
