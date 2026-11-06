#pragma once
#include "Common/MyWindows.h"
#include "archive/password/archivepassword.h"
#include "archive/resources/qtarchiveupdatepublication.h"
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

class CPanel;
class ArchiveBackend;
class QObject;
struct CTempFileInfo;
class QWidget;

// Native entry paths and ordinary password state; no retained reader or window.
class QtExternalArchiveTarget final {
  public:
    struct UpdateResult {
        HRESULT Result;
        std::optional<bool> Published;
        QStringList retainedInputs;
    };
    static std::optional<QtExternalArchiveTarget> Capture(CPanel &source, const CTempFileInfo &file,
                                                          const ArchiveBackend &backend,
                                                          const QString &settingsPath);
    UpdateResult Update(const QString &file, QWidget *parent) const;
    const QString &physicalPath() const { return physical_; }
    QString description() const;
    QJsonObject Save() const;
    static std::optional<QtExternalArchiveTarget>
    Restore(const QJsonObject &value, const ArchiveBackend &backend, const QString &settingsPath);

  private:
    struct Entry {
        UInt32 index;
        UString path;
    };
    struct Layer {
        Entry entry;
        QString format;
        bool usePassword;
        ArchivePassword password;
    };
    QString physical_, settings_;
    std::vector<Layer> layers_;
    UInt32 version_ = 0;
    std::function<ArchiveBackend *(QObject *)> backendFactory_;
};
