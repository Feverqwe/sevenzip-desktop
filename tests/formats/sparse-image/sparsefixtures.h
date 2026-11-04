#pragma once
#include <QJsonObject>
#include <QString>
class QTemporaryDir;
QJsonObject SparseFixtureManifest();
QString UnpackSparseFixture(const QJsonObject &fixture, QTemporaryDir &scratch);
QString DamagedSparseBundle(QTemporaryDir &scratch);
QString DirectorySparseBundle(QTemporaryDir &scratch);
