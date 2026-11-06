#pragma once
#include <QString>
#include <QVector>
#include <optional>

// Documents come from the installed payload, independently of help or codecs.
class QtLicenseDocuments final {
  public:
    struct Document {
        QString title;
        QString path;
    };
    explicit QtLicenseDocuments(QString directory);
    static QString InstalledDirectory();
    bool load(QString &error);
    const QVector<Document> &documents() const { return documents_; }
    std::optional<QString> read(int row, QString &error) const;

  private:
    QString resolve(const QString &relative, QString &error) const;
    QString directory_;
    QVector<Document> documents_;
};
