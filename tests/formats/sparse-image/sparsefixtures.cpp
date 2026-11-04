#include "sparsefixtures.h"
#include "formats/encrypted-dmg/dmgfixtures.h"
#include "formats/exfat/support/exfat_fixture.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

namespace {
QByteArray Read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool Write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).absolutePath()) && file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size();
}
} // namespace
QJsonObject SparseFixtureManifest() {
    return QJsonDocument::fromJson(
               Read(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) + "/provenance.json"))
        .object();
}
QString UnpackSparseFixture(const QJsonObject &fixture, QTemporaryDir &scratch) {
    const auto name = fixture["name"].toString();
    const bool bundle = fixture["type"].toString() == "SPARSEBUNDLE";
    const auto path = scratch.filePath(QString::fromUtf8("образ 空 ") + name +
                                       (bundle ? ".sparsebundle" : ".sparseimage"));
    for (const auto &value : fixture["files"].toArray()) {
        const auto relative = value.toObject()["path"].toString();
        const auto compressed =
            Read(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) + '/' + name + '/' + relative + ".z");
        const auto output = bundle ? QDir(path).filePath(relative) : path;
        if (!Write(output, qUncompress(compressed)))
            return {};
    }
    return path;
}
QString DamagedSparseBundle(QTemporaryDir &scratch) {
    const ExfatFixture volume;
    const auto path = scratch.filePath(QString::fromUtf8("повреждённый 空.sparsebundle"));
    const QByteArray plist = "<plist><dict><key>diskimage-bundle-type</"
                             "key><string>com.apple.diskimage.sparsebundle</string>"
                             "<key>band-size</key><integer>4096</integer><key>size</key><integer>" +
                             QByteArray::number(volume.image.size()) + "</integer></dict></plist>";
    if (!Write(QDir(path).filePath("Info.plist"), plist) ||
        !Write(QDir(path).filePath("token"), DmgFixture({}, 256, true)))
        return {};
    for (qsizetype pos = 0; pos < volume.image.size(); pos += 4096) {
        auto bytes = DmgFixture(volume.image.mid(pos, 4096), 256, true).mid(4096);
        if (pos == 32768)
            bytes.truncate(
                3665); // first fragment survives, second fragment crosses a torn AES block
        if (!Write(QDir(path).filePath("bands/" + QString::number(pos / 4096, 16)), bytes))
            return {};
    }
    return path;
}

QString DirectorySparseBundle(QTemporaryDir &scratch) {
    const auto image =
        qUncompress(Read(QString::fromUtf8(SEVENZIP_SPARSE_FIXTURES) + "/apfs-directories.img.z"));
    if (image.isEmpty())
        return {};
    const auto path = scratch.filePath(QString::fromUtf8("каталоги 空.sparsebundle"));
    constexpr qsizetype bandSize = 8u << 20;
    const QByteArray plist =
        "<plist><dict><key>diskimage-bundle-type</key>"
        "<string>com.apple.diskimage.sparsebundle</string><key>band-size</key><integer>" +
        QByteArray::number(bandSize) + "</integer><key>size</key><integer>" +
        QByteArray::number(image.size()) + "</integer></dict></plist>";
    if (!Write(path + "/Info.plist", plist) || !Write(path + "/token", DmgFixture({}, 256, true)))
        return {};
    for (qsizetype pos = 0; pos < image.size(); pos += bandSize) {
        const auto bytes = image.mid(pos, bandSize);
        if (bytes == QByteArray(bytes.size(), '\0'))
            continue;
        if (!Write(path + "/bands/" + QString::number(pos / bandSize, 16),
                   DmgFixture(bytes, 256, true).mid(4096)))
            return {};
    }
    return path;
}
