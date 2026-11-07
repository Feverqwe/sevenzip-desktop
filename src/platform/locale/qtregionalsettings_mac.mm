#include "platform/locale/qtregionalsettings.h"
#import <Foundation/Foundation.h>

namespace {
QString FormatNumber(const QString &decimal, int precision) {
    @autoreleasepool {
        auto *format = [[NSNumberFormatter alloc] init];
        format.locale = [NSLocale autoupdatingCurrentLocale];
        format.numberStyle = NSNumberFormatterDecimalStyle;
        format.usesGroupingSeparator = YES;
        format.minimumFractionDigits = precision;
        format.maximumFractionDigits = precision;
        // Decimal input preserves every UInt64 digit; NSNumber(double) would
        // lose low bits for large exact byte counts.
        auto *number = [NSDecimalNumber
            decimalNumberWithString:decimal.toNSString()
                             locale:[NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"]];
        return QString::fromNSString([format stringFromNumber:number]);
    }
}

class MacRegionalNotification final : public QtRegionalNotification {
  public:
    explicit MacRegionalNotification(std::function<void()> changed) {
        auto *center = [NSNotificationCenter defaultCenter];
        locale_ = [center addObserverForName:NSCurrentLocaleDidChangeNotification
                                      object:nil
                                       queue:[NSOperationQueue mainQueue]
                                  usingBlock:^(NSNotification *) {
                                    changed();
                                  }];
        zone_ = [center addObserverForName:NSSystemTimeZoneDidChangeNotification
                                    object:nil
                                     queue:[NSOperationQueue mainQueue]
                                usingBlock:^(NSNotification *) {
                                  changed();
                                }];
    }
    ~MacRegionalNotification() override {
        auto *center = [NSNotificationCenter defaultCenter];
        [center removeObserver:locale_];
        [center removeObserver:zone_];
    }

  private:
    id locale_ = nil;
    id zone_ = nil;
};
} // namespace

std::unique_ptr<QtRegionalNotification>
QtCreateRegionalNotification(std::function<void()> changed) {
    return std::make_unique<MacRegionalNotification>(std::move(changed));
}

QLocale QtSystemRegionalUnitsLocale() {
    return QLocale(QString::fromNSString([NSLocale autoupdatingCurrentLocale].localeIdentifier));
}

QString QtSystemRegionalNumber(quint64 value) {
    return FormatNumber(QString::number(qulonglong(value)), 0);
}

QString QtSystemRegionalNumber(double value, int precision) {
    return FormatNumber(QString::number(value, 'f', precision), precision);
}

QString QtSystemRegionalDateTime(const QDateTime &local, QLocale::FormatType format) {
    return QLocale::system().toString(local, format);
}
