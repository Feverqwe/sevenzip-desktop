#include "fileops/volumes/qtvolumeprogress.h"
#include "filemanager/panels/PanelSplitFile.h"
#include "ui/progress/progresssync.h"

struct QtVolumeProgress::State {
    CProgressSync owned;
    CProgressSync *sync = &owned;
};

QtVolumeProgress::QtVolumeProgress() : state_(std::make_unique<State>()) {}
QtVolumeProgress::~QtVolumeProgress() = default;
void QtVolumeProgress::attach(CProgressSync &sync) { state_->sync = &sync; }
void QtVolumeProgress::cancel() { state_->sync->Set_Stopped(true); }
void QtVolumeProgress::Set_NumBytesTotal(UInt64 value) { state_->sync->Set_NumBytesTotal(value); }
void QtVolumeProgress::Set_NumFilesCur(UInt64 value) { state_->sync->Set_NumFilesCur(value); }
void QtVolumeProgress::Set_FilePath(const UString &path) { state_->sync->Set_FilePath(path); }
HRESULT QtVolumeProgress::Set_NumBytesCur(UInt64 value) {
    if (advanced)
        advanced(value);
    return state_->sync->Set_NumBytesCur(value);
}

bool QtVolumeWorker::CreateOutput(NWindows::NFile::NIO::COutFile &file, const FString &path) {
    return createOutput ? createOutput(file, path) : file.Create_NEW(path);
}
