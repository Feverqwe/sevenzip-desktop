#include "platform/shell/wincontextmenuservices.h"
#include "7zip/UI/Explorer/MyMessages.h"
#include "Windows/ProcessUtils.h"
#include "operations/CompressCall.h"
#include "platform/loader/DLL.h"

namespace {
class NativeContextMenuServices final : public IQtContextMenuServices {
  public:
    void Open(const UString &path, const UString &type) override {
        // Original ContextMenu::InvokeCommandCommon kOpen body.
        UString params = GetQuotedString(path);
        if (!type.IsEmpty()) {
            params += " -t";
            params += type;
        }
        const UString image = fs2us(NWindows::NDLL::GetModuleDirPrefix()) + L"7zFM.exe";
        NWindows::MyCreateProcess(image, params);
    }
    void ExtractArchives(const UStringVector &paths, const UString &folder, bool showDialog,
                         bool elimDup, UInt32 writeZone) override {
        ::ExtractArchives(paths, folder, showDialog, elimDup, writeZone);
    }
    void TestArchives(const UStringVector &paths, bool hashMode) override {
        ::TestArchives(paths, hashMode);
    }
    void CompressFiles(const UString &folder, const UString &name, const UString &type,
                       bool addExtension, const UStringVector &paths, bool email, bool showDialog,
                       bool waitFinish) override {
        ::CompressFiles(folder, name, type, addExtension, paths, email, showDialog, waitFinish);
    }
    void CalcChecksum(const UStringVector &paths, const UString &method) override {
        ::CalcChecksum(paths, method);
    }
    void ShowErrorMessage(const UString &message) override { ::ShowErrorMessage(nullptr, message); }
    void ShowErrorMessageRes(UInt32 resourceID) override { ::ShowErrorMessageRes(resourceID); }
};
} // namespace

std::unique_ptr<IQtContextMenuServices> CreateNativeContextMenuServices() {
    return std::make_unique<NativeContextMenuServices>();
}
