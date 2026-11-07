#include "platform/locale/qtregionalsettings.h"
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <cwchar>
#include <string>
#include <windows.h>

namespace {
QString LocaleField(LCTYPE type) {
    const int size = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring field(size_t(size), L'\0');
    if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, type, field.data(), size))
        return {};
    return QString::fromWCharArray(field.data(), size - 1);
}

QString NativeDigits(const QString &text) {
    // NLS returns nominal digits; honor the user's explicit national-digit
    // preference before Qt presents the result. Contextual shaping remains
    // with the text renderer.
    if (LocaleField(LOCALE_IDIGITSUBSTITUTION) != "2")
        return text;
    const auto digits = LocaleField(LOCALE_SNATIVEDIGITS).toUcs4();
    if (digits.size() != 10)
        return text;
    QString result;
    result.reserve(text.size());
    for (const QChar character : text) {
        const auto code = character.unicode();
        if (code >= u'0' && code <= u'9')
            result += QString::fromUcs4(&digits[code - u'0'], 1);
        else
            result += character;
    }
    return result;
}

QString FormatNumber(const QString &decimal, unsigned precision) {
    auto point = LocaleField(LOCALE_SDECIMAL).toStdWString();
    auto separator = LocaleField(LOCALE_STHOUSAND).toStdWString();
    const auto groups = LocaleField(LOCALE_SGROUPING).split(';');
    unsigned grouping = 0;
    // NLS's semicolon form ends in zero for a repeating final group;
    // NUMBERFMT's integer form uses the reverse convention for that terminator.
    for (const auto &group : groups)
        if (group != "0")
            grouping = grouping * 10 + group.toUInt();
    if (!groups.isEmpty() && groups.last() != "0")
        grouping *= 10;
    NUMBERFMTW format{};
    format.NumDigits = precision;
    format.LeadingZero = 1;
    format.Grouping = grouping;
    format.lpDecimalSep = point.data();
    format.lpThousandSep = separator.data();
    const auto input = decimal.toStdWString();
    const int size =
        GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, input.c_str(), &format, nullptr, 0);
    if (size <= 0)
        return decimal;
    std::wstring result(size_t(size), L'\0');
    if (!GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, input.c_str(), &format, result.data(),
                           size))
        return decimal;
    return NativeDigits(QString::fromWCharArray(result.data(), size - 1));
}

class WindowsRegionalNotification final : public QtRegionalNotification,
                                          public QAbstractNativeEventFilter {
  public:
    explicit WindowsRegionalNotification(std::function<void()> changed)
        : changed_(std::move(changed)) {
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
    ~WindowsRegionalNotification() override {
        if (auto *application = QCoreApplication::instance())
            application->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
        const auto &native = *static_cast<MSG *>(message);
        if (native.message == WM_TIMECHANGE)
            changed_();
        if (native.message == WM_SETTINGCHANGE) {
            const auto *area = reinterpret_cast<const wchar_t *>(native.lParam);
            if (!area || std::wcscmp(area, L"intl") == 0 ||
                std::wcscmp(area, L"TimeZoneInformation") == 0)
                changed_();
        }
        return false;
    }

  private:
    std::function<void()> changed_;
};
} // namespace

std::unique_ptr<QtRegionalNotification>
QtCreateRegionalNotification(std::function<void()> changed) {
    return std::make_unique<WindowsRegionalNotification>(std::move(changed));
}

QLocale QtSystemRegionalUnitsLocale() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH];
    return GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0
               ? QLocale(QString::fromWCharArray(name))
               : QLocale::system();
}

QString QtSystemRegionalNumber(quint64 value) {
    return FormatNumber(QString::number(qulonglong(value)), 0);
}

QString QtSystemRegionalNumber(double value, int precision) {
    return FormatNumber(QString::number(value, 'f', precision), unsigned(precision));
}

QString QtSystemRegionalDateTime(const QDateTime &local, QLocale::FormatType format) {
    const auto date = local.date();
    const auto time = local.time();
    if (!local.isValid() || date.year() < 1 || date.year() > 65535)
        return {};
    SYSTEMTIME native{};
    native.wYear = WORD(date.year());
    native.wMonth = WORD(date.month());
    native.wDay = WORD(date.day());
    native.wDayOfWeek = WORD(date.dayOfWeek() % 7);
    native.wHour = WORD(time.hour());
    native.wMinute = WORD(time.minute());
    native.wSecond = WORD(time.second());
    native.wMilliseconds = WORD(time.msec());
    const DWORD dateFlags = format == QLocale::ShortFormat ? DATE_SHORTDATE : DATE_LONGDATE;
    const int dateSize =
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, dateFlags, &native, nullptr, nullptr, 0, nullptr);
    const auto timePattern =
        LocaleField(format == QLocale::ShortFormat ? LOCALE_SSHORTTIME : LOCALE_STIMEFORMAT)
            .toStdWString();
    const int timeSize =
        GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &native, timePattern.c_str(), nullptr, 0);
    if (dateSize <= 0 || timeSize <= 0)
        return {};
    std::wstring dateText(size_t(dateSize), L'\0'), timeText(size_t(timeSize), L'\0');
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, dateFlags, &native, nullptr, dateText.data(),
                         dateSize, nullptr) ||
        !GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &native, timePattern.c_str(), timeText.data(),
                         timeSize))
        return {};
    return NativeDigits(QString::fromWCharArray(dateText.data(), dateSize - 1) + ' ' +
                        QString::fromWCharArray(timeText.data(), timeSize - 1));
}
