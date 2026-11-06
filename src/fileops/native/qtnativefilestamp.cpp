#include "fileops/native/qtnativefilestamp.h"

QJsonObject QtNativeFileStamp::Save() const {
    QJsonObject result{{"size", QString::number(size)}};
#ifdef _WIN32
    result.insert("filetime", QString::number((UInt64(modified.dwHighDateTime) << 32) |
                                              modified.dwLowDateTime));
#else
    result.insert("seconds", QString::number(modified.tv_sec));
    result.insert("nanoseconds", int(modified.tv_nsec));
#endif
    return result;
}

std::optional<QtNativeFileStamp> QtNativeFileStamp::Restore(const QJsonObject &value) {
    bool valid = false;
    const UInt64 size = value.value("size").toString().toULongLong(&valid);
    if (!valid)
        return {};
    CFiTime modified{};
#ifdef _WIN32
    const UInt64 ticks = value.value("filetime").toString().toULongLong(&valid);
    if (!valid || value.contains("seconds"))
        return {};
    modified.dwHighDateTime = UInt32(ticks >> 32);
    modified.dwLowDateTime = UInt32(ticks);
#else
    const qint64 seconds = value.value("seconds").toString().toLongLong(&valid);
    const auto nanos = value.value("nanoseconds");
    const int nanoseconds = nanos.toInt(-1);
    if (!valid || value.contains("filetime") || !nanos.isDouble() || nanoseconds < 0 ||
        nanoseconds >= 1000000000 || nanos.toDouble() != nanoseconds)
        return {};
    modified.tv_sec = decltype(modified.tv_sec)(seconds);
    modified.tv_nsec = decltype(modified.tv_nsec)(nanoseconds);
    if (qint64(modified.tv_sec) != seconds)
        return {};
#endif
    return QtNativeFileStamp{size, modified};
}
