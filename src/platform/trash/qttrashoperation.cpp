#include "platform/trash/qttrashoperation.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QCoreApplication>
#ifdef _WIN32
#include "Windows/COM.h"
#endif

namespace {
class TrashOperation final : public CProgressThreadVirt {
    const UStringVector &paths_;
    IQtTrashService &trash_;

    HRESULT ProcessVirt() override {
#ifdef _WIN32
        NWindows::NCOM::CComInitializer comInitializer;
#endif
        Sync.Set_FilesProgressMode(true);
        RINOK(Sync.Set_NumFilesTotal(paths_.Size()))
        FOR_VECTOR(i, paths_) {
            RINOK(Sync.CheckStop())
            const QString path = QString::fromWCharArray(paths_[i]);
            Sync.Set_FilePath(paths_[i]);
            if (!trash_.MoveToTrash(path)) {
                const QString message = QCoreApplication::translate(
                    "DeleteRedesign", "Could not move the item to Trash.");
                FinalMessage.ErrorMessage.Message = message.toStdWString().c_str();
                AddErrorPath(us2fs(paths_[i]));
                return E_FAIL;
            }
            Sync.Set_NumFilesCur(++MovedCount);
        }
        return S_OK;
    }

  public:
    unsigned MovedCount = 0;
    TrashOperation(const UStringVector &paths, IQtTrashService &trash)
        : paths_(paths), trash_(trash) {
        ShowCompressionInfo = false;
    }
};
} // namespace

HRESULT QtMoveItemsToTrash(const UStringVector &paths, IQtTrashService &trash, QWidget *parent,
                           bool deferResultReview, QStringList *diagnostics, unsigned *movedCount) {
    TrashOperation operation(paths, trash);
    operation.DeferResultReview = deferResultReview;
    operation.WaitMode = true;
    operation.MainWindow = parent;
    operation.MainTitle = L"7-Zip";
    operation.MainAddTitle = LangString(IDS_DELETING);
    QStringList sources;
    FOR_VECTOR(i, paths)
    sources.append(QString::fromWCharArray(paths[i]));
    operation.SetLocations(sources.join('\n'));
    operation.Sync.FinalMessage.ErrorMessage.Title = LangString(IDS_ERROR_DELETING);
    const HRESULT started = operation.Create(LangString(IDS_DELETING), parent);
    if (movedCount)
        *movedCount = operation.MovedCount;
    RINOK(started)
    if (diagnostics) {
        FOR_VECTOR(i, operation.Sync.Messages)
        diagnostics->append(QString::fromWCharArray(operation.Sync.Messages[i]));
        if (!operation.Sync.FinalMessage.ErrorMessage.Message.IsEmpty())
            diagnostics->append(
                QString::fromWCharArray(operation.Sync.FinalMessage.ErrorMessage.Message));
    }
    return operation.Result;
}
