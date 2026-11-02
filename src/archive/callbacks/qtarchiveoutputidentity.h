#pragma once
#include <QHash>
#include <QSet>
#include <QString>
#include <optional>

// One extraction's outputs must remain distinct from other selected entries.
class QtArchiveOutputIdentity {
    QHash<QString, QString> logicalToPhysical_;
    QHash<QString, QString> physicalToLogical_;
    QSet<QString> outputPaths_, outputIdentities_;

  public:
    bool Record(const QString &path, const QString &logical);
    std::optional<bool> WasOutput(const QString &path) const;
    bool RecordOutput(const QString &path);
    void Clear();
};
