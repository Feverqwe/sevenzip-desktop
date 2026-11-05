#include "fileops/volumes/volumeoperations.h"
#include "7zip/UI/FileManager/resource.h"
#include "filemanager/FormatUtils.h"
#include "filemanager/SplitUtils.h"
#include "fileops/volumes/nativevolumeworker.h"
#include "fileops/volumes/qtvolumetransaction.h"
#include "localization/LangUtils.h"
#include "localization/qtlangcontrols.h"
#include "localization/qtlanguageservices.h"
#include "settings/ViewSettings.h"
#include "ui/diagnostics/qtdiagnostictext.h"
#include "ui/progress/ProgressDialog2.h"
#include <QDir>
#include <QFileInfo>
#include <limits>
#include <type_traits>

namespace {
struct InputSnapshot {
    QString path;
    qint64 size;

    bool unchanged() const {
        const QFileInfo current(path);
        return current.isFile() && !current.isSymLink() && current.size() == size;
    }
};
} // namespace

VolumeOperation::VolumeOperation(Mode mode, QString source, QString destination, qint64 volumeSize,
                                 QObject *parent)
    : VolumeOperation(mode, std::move(source), std::move(destination), QList<qint64>{volumeSize},
                      parent) {}
VolumeOperation::VolumeOperation(Mode mode, QString source, QString destination,
                                 QList<qint64> volumeSizes, QObject *parent)
    : QThread(parent), source_(std::move(source)), destination_(std::move(destination)) {
    if (mode == Mode::Combine)
        plan_.emplace<CCombineVolumePlan>();
    else
        for (qint64 size : volumeSizes) {
            invalidSizes_ |= size <= 0;
            std::get<CSplitVolumePlan>(plan_).VolumeSizes.Add(UInt64(size));
        }
}
VolumeOperation::VolumeOperation(const CSplitVolumePlan &plan, QObject *parent)
    : QThread(parent), plan_(plan), source_(ToQString(fs2us(plan.FilePath))),
      destination_(QFileInfo(ToQString(fs2us(plan.VolBasePath))).absolutePath()),
      nativePlan_(true) {}
VolumeOperation::VolumeOperation(const CCombineVolumePlan &plan, QObject *parent)
    : QThread(parent), plan_(plan),
      source_(ToQString(fs2us(plan.InputDirPrefix + plan.Names.Front()))),
      destination_(ToQString(fs2us(plan.OutputPath))), nativePlan_(true) {}
void VolumeOperation::cancel() { canceled_ = true; }

QList<qint64> VolumeOperation::parseSizes(const QString &text) {
    CRecordVector<UInt64> values;
    if (!ParseVolumeSizesText(text, values) || values.IsEmpty())
        return {};
    QList<qint64> result;
    FOR_VECTOR(i, values) {
        if (values[i] == 0 || values[i] > UInt64(std::numeric_limits<qint64>::max()))
            return {};
        result.append(qint64(values[i]));
    }
    return result;
}
qint64 VolumeOperation::parseSize(const QString &text) {
    const auto sizes = parseSizes(text);
    return sizes.size() == 1 ? sizes.first() : 0;
}

namespace {
bool InspectCombinePlan(const QString &firstPart, CCombineVolumePlan &combiner, QString *error) {
    if (firstPart.contains(QChar::Null)) {
        *error = ToQString(HResultToMessage(E_INVALIDARG));
        return false;
    }
    const QFileInfo first(firstPart);
    CVolSeqName sequence;
    if (!sequence.ParseName(ToUString(first.fileName()))) {
        *error = ToQString(LangString(IDS_COMBINE_CANT_DETECT_SPLIT_FILE));
        return false;
    }
    const UString prefix = ToUString(first.absolutePath() + '/');
    combiner.InputDirPrefix = us2fs(prefix);
    combiner.LoadParts(prefix, ToUString(first.fileName()), sequence);
    if (combiner.Names.Size() == 1) {
        *error = ToQString(LangString(IDS_COMBINE_CANT_FIND_MORE_THAN_ONE_PART));
        return false;
    }
    if (combiner.TotalSize == 0) {
        *error = QCoreApplication::translate("volumeoperations", "No data");
        return false;
    }
    FOR_VECTOR(i, combiner.Names) {
        const QString path = ToQString(prefix + fs2us(combiner.Names[i]));
        if (QFileInfo(path).isSymLink()) {
            *error = QCoreApplication::translate("volumeoperations", "File part is unavailable: %1")
                         .arg(path);
            return false;
        }
    }
    if (combiner.TotalSize > UInt64(std::numeric_limits<qint64>::max())) {
        *error = QCoreApplication::translate("volumeoperations",
                                             "Part sizes are unavailable or too large.");
        return false;
    }
    return true;
}
} // namespace

VolumeParts VolumeOperation::inspectParts(const QString &firstPart, QString *error) {
    CCombineVolumePlan plan;
    if (!InspectCombinePlan(firstPart, plan, error))
        return {};
    VolumeParts result;
    FOR_VECTOR(i, plan.Names)
    result.paths << ToQString(fs2us(plan.InputDirPrefix + plan.Names[i]));
    result.totalSize = qint64(plan.TotalSize);
    return result;
}
QStringList VolumeOperation::discoverParts(const QString &firstPart, QString *error) {
    return inspectParts(firstPart, error).paths;
}

template <class Worker> bool VolumeOperation::process(Worker &worker, QString *error) {
    if (progressSync_)
        worker.Sync.attach(*progressSync_);
    worker.Sync.advanced = [this, &worker](UInt64 pos) {
        emit advanced(qint64(pos));
        if (canceled_)
            worker.Sync.cancel();
    };
    if (canceled_)
        worker.Sync.cancel();
    HRESULT result;
    try {
        result = worker.ProcessVirt();
    } catch (const char *message) {
        *error = QString::fromWCharArray(QtDiagnosticText(message));
        return false;
    } catch (...) {
        *error = QCoreApplication::translate("volumeoperations", "Volume operation failed");
        return false;
    }
    if (canceled_ || result == E_ABORT) {
        *error = QCoreApplication::translate("volumeoperations", "Operation canceled.");
        return false;
    }
    if (result != S_OK) {
        if constexpr (std::is_same_v<Worker, CThreadCombine>)
            if (!worker.ErrorPaths.IsEmpty()) {
                *error =
                    QCoreApplication::translate("volumeoperations", "File part is unavailable: %1")
                        .arg(ToQString(worker.ErrorPaths.Back()));
                return false;
            }
        *error = ToQString(HResultToMessage(result));
        if (!worker.ErrorPaths.IsEmpty())
            *error += '\n' + ToQString(worker.ErrorPaths.Back());
        return false;
    }
    return true;
}

bool VolumeOperation::split(const CSplitVolumePlan &plan, QString *error) {
    const QFileInfo source(source_);
    CThreadSplit spliter(plan);
    if (invalidSizes_) {
        *error = QCoreApplication::translate("volumeoperations", "Volume size must be positive.");
        return false;
    }
    if (!source.isFile() || source.isSymLink() || !QFileInfo(destination_).isDir() ||
        QFileInfo(destination_).isSymLink() || spliter.VolumeSizes.IsEmpty() ||
        UInt64(source.size()) <= spliter.VolumeSizes.FrontItem()) {
        *error = QCoreApplication::translate("volumeoperations",
                                             "File or volume size is unavailable for splitting.");
        return false;
    }
    if (!nativePlan_) {
        spliter.FilePath = us2fs(ToUString(source_));
        spliter.VolBasePath = us2fs(ToUString(QDir(destination_).filePath(source.fileName())));
        spliter.NumVolumes = GetNumberOfVolumes(UInt64(source.size()), spliter.VolumeSizes);
    }
    const InputSnapshot input{source_, source.size()};
    QtVolumeTransaction transaction;
    spliter.createOutput = [&transaction](auto &file, const FString &path) {
        return transaction.create(file, path);
    };
    if (!process(spliter, error))
        return false;
    if (!input.unchanged()) {
        *error =
            QCoreApplication::translate("volumeoperations", "Source file changed while splitting.");
        return false;
    }
    if (!transaction.publish()) {
        *error = QCoreApplication::translate("volumeoperations", "Could not save part: %1")
                     .arg(transaction.failedPath());
        return false;
    }
    return true;
}

bool VolumeOperation::combine(const CCombineVolumePlan &plan, QString *error) {
    CThreadCombine combiner(plan);
    if (!nativePlan_) {
        if (!InspectCombinePlan(source_, combiner, error))
            return false;
        combiner.OutputPath = us2fs(ToUString(destination_));
    }
    if (combiner.Names.IsEmpty())
        return false;
    const QFileInfo target(destination_);
    if (!QFileInfo(target.absolutePath()).isDir() || QFileInfo::exists(destination_) ||
        target.isSymLink()) {
        *error = QCoreApplication::translate(
            "volumeoperations", "Destination folder is unavailable or the file already exists.");
        return false;
    }
    QList<InputSnapshot> inputs;
    FOR_VECTOR(i, combiner.Names) {
        const QString path = ToQString(fs2us(combiner.InputDirPrefix + combiner.Names[i]));
        const QFileInfo input(path);
        if (!input.isFile() || input.isSymLink()) {
            *error = QCoreApplication::translate("volumeoperations", "File part is unavailable: %1")
                         .arg(path);
            return false;
        }
        inputs.append({path, input.size()});
    }
    QtVolumeTransaction transaction;
    combiner.createOutput = [&transaction](auto &file, const FString &path) {
        return transaction.create(file, path);
    };
    if (!process(combiner, error))
        return false;
    for (const auto &input : inputs) {
        if (!input.unchanged()) {
            *error = QCoreApplication::translate("volumeoperations", "File part changed: %1")
                         .arg(input.path);
            return false;
        }
    }
    if (!transaction.publish()) {
        *error = QCoreApplication::translate("volumeoperations", "Could not save combined file.");
        return false;
    }
    return true;
}

void VolumeOperation::run() {
    if (source_.contains(QChar::Null) || destination_.contains(QChar::Null)) {
        emit result(false, ToQString(HResultToMessage(E_INVALIDARG)));
        return;
    }
    QString error;
    const auto *splitPlan = std::get_if<CSplitVolumePlan>(&plan_);
    const bool ok = splitPlan ? split(*splitPlan, &error)
                              : combine(std::get<CCombineVolumePlan>(plan_), &error);
    emit result(ok, ok ? (splitPlan
                              ? QCoreApplication::translate("volumeoperations", "File split.")
                              : QCoreApplication::translate("volumeoperations", "Files combined."))
                       : error);
}
