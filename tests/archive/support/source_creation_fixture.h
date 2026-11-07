#pragma once

#include "archive/sfx/archivesfxmodule.h"
#include "archive/support/native_archive_fixture.h"
#include "dialogs/compress/CompressDialog.h"
#include "gui/UpdateCallbackGUI.h"
#include "gui/UpdateGUI.h"
#include "gui/qtupdateguiservices.h"
#include "ui/progress/ProgressDialog2.h"

// Exercise the same source dialog-to-writer boundary as GUI creation, with
// deterministic dialog input and no console process or interactive window.
namespace SourceCreationFixture {
class Input final : public IQtUpdateGUIServices {
  public:
    NCompressDialog::CInfo Info;
    HRESULT PresentCompressDialog(CCompressDialog &dialog,
                                  const CObjectVector<NWildcard::CCensorPath> &,
                                  QWidget *) override {
        dialog.Info = Info;
        return S_OK;
    }
    HRESULT UpdateArchive(CCodecs *, const CObjectVector<COpenType> &, const UString &,
                          NWildcard::CCensor &, CUpdateOptions &, CUpdateErrorInfo &,
                          IOpenCallbackUI *, IUpdateCallbackUI2 *, bool) override {
        return E_NOTIMPL;
    }
};
enum class OptionsOwner { Graphical, Native };

inline HRESULT Create(ArchiveBackend &backend, const QString &target, const QStringList &sources,
                      const QString &password = {}, bool encryptHeaders = false,
                      const CompressionOptions &info = {}, bool cancelled = false,
                      OptionsOwner owner = OptionsOwner::Graphical) {
    if (!backend.codecCatalog()) {
        const HRESULT attached = NativeArchiveFixture::Attach(backend);
        if (attached != S_OK)
            return attached;
    }
    const auto catalog = backend.codecCatalog();
    const QString type = info.format.isEmpty() ? QFileInfo(target).suffix() : info.format;
    const int index = catalog->formatIndex(type);
    if (index < 0 || !catalog->canUpdate(type))
        return E_NOTIMPL;
    CObjectVector<CProperty> properties;
    QString errorMessage;
    if (!ArchiveBackend::compressionProperties(type, password, encryptHeaders, info, &properties,
                                               &errorMessage))
        return E_INVALIDARG;
    NWildcard::CCensor censor;
    for (const auto &source : sources)
        censor.AddPreItem_NoWildcard(source.toStdWString().c_str());
    CUpdateOptions options;
    options.SetActionCommand_Add();
    options.MethodMode.Type_Defined = true;
    options.MethodMode.Type.FormatIndex = index;
    CProgressDialog progress;
    progress.Sync.Set_Stopped(cancelled);
    CUpdateCallbackGUI callback;
    callback.ProgressDialog = &progress;
    callback.Init();
    Input input;
    input.Info = info;
    input.Info.FormatIndex = index;
    input.Info.ArcPath = target.toStdWString().c_str();
    input.Info.Password = password;
    input.Info.EncryptHeaders = encryptHeaders;
    RINOK(ShowDialog(const_cast<CCodecs *>(&catalog->nativeCodecs()), censor.CensorPaths, options,
                     &callback, nullptr, input))
    options.MethodMode.Properties = properties;
    if (owner == OptionsOwner::Native) {
        options.DeleteAfterCompressing = info.DeleteAfterCompressing;
        options.SfxMode = info.SFXMode;
        if (options.SfxMode) {
#ifdef _WIN32
            options.ArchivePath.BaseExtension = "exe";
#else
            options.ArchivePath.BaseExtension.Empty();
#endif
            options.ArchivePath.ParseFromPath(input.Info.ArcPath, k_ArcNameMode_Smart);
        }
    }
    options.ArcNameMode = options.SfxMode ? k_ArcNameMode_Smart : k_ArcNameMode_Exact;
    if (options.SfxMode) {
        const auto module = backend.sfxModule(info.SfxModule);
        if (!module.available())
            return E_FAIL;
        options.SfxModule = us2fs(module.path().toStdWString().c_str());
    }
    CUpdateErrorInfo error;
    return backend.updateSourceArchive(target, type, censor, options, error, &callback, &callback,
                                       true);
}
} // namespace SourceCreationFixture
