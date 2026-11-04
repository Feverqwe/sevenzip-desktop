#pragma once

#include "fileops/volumes/nativevolumeworker.h"
#include <QStringList>
#include <QThread>
#include <atomic>
#include <variant>

struct VolumeParts {
    QStringList paths;
    qint64 totalSize = 0;
};
class CProgressSync;

class VolumeOperation : public QThread {
    Q_OBJECT

  public:
    enum class Mode { Split, Combine };
    VolumeOperation(Mode mode, QString source, QString destination, qint64 volumeSize = 0,
                    QObject *parent = nullptr);
    VolumeOperation(Mode mode, QString source, QString destination, QList<qint64> volumeSizes,
                    QObject *parent = nullptr);
    VolumeOperation(const CSplitVolumePlan &plan, QObject *parent = nullptr);
    VolumeOperation(const CCombineVolumePlan &plan, QObject *parent = nullptr);
    void cancel();
    bool isCanceled() const { return canceled_.load(); }
    void attachProgress(CProgressSync &sync) { progressSync_ = &sync; }
    static QList<qint64> parseSizes(const QString &text);
    static qint64 parseSize(const QString &text);
    static VolumeParts inspectParts(const QString &firstPart, QString *error);
    static QStringList discoverParts(const QString &firstPart, QString *error);

  signals:
    void advanced(qint64 bytes);
    void result(bool success, const QString &message);

  protected:
    void run() override;

  private:
    template <class Worker> bool process(Worker &worker, QString *error);
    bool split(const CSplitVolumePlan &plan, QString *error);
    bool combine(const CCombineVolumePlan &plan, QString *error);
    std::variant<CSplitVolumePlan, CCombineVolumePlan> plan_;
    QString source_;
    QString destination_;
    bool nativePlan_ = false;
    bool invalidSizes_ = false;
    CProgressSync *progressSync_ = nullptr;
    std::atomic_bool canceled_{false};
};
