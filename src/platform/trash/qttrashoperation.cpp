#include "platform/trash/qttrashoperation.h"
#include "7zip/UI/FileManager/resource.h"
#include "Common/StringConvert.h"
#include "localization/LangUtils.h"
#include "platform/trash/qttrashservice.h"
#include "ui/progress/ProgressDialog2.h"
#include <QFileInfo>
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
            if (QFileInfo(path).isSymLink()) {
                AddErrorPath(us2fs(paths_[i]));
                return E_NOTIMPL;
            }
            if (!trash_.MoveToTrash(path)) {
                AddErrorPath(us2fs(paths_[i]));
                return E_FAIL;
            }
            Sync.Set_NumFilesCur(i + 1);
        }
        return Sync.CheckStop();
    }

  public:
    TrashOperation(const UStringVector &paths, IQtTrashService &trash)
        : paths_(paths), trash_(trash) {
        ShowCompressionInfo = false;
        ShowByteStatistics(false);
    }
};
} // namespace

HRESULT QtMoveItemsToTrash(const UStringVector &paths, IQtTrashService &trash, QWidget *parent) {
    TrashOperation operation(paths, trash);
    operation.WaitMode = true;
    operation.MainWindow = parent;
    operation.MainTitle = L"7-Zip";
    operation.MainAddTitle = LangString(IDS_DELETING);
    operation.Sync.FinalMessage.ErrorMessage.Title = LangString(IDS_ERROR_DELETING);
    RINOK(operation.Create(LangString(IDS_DELETING), parent))
    return operation.Result;
}
