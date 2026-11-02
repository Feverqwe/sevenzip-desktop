// Test-only facade over the unchanged official 7z module. All archive work is
// delegated; withholding ISetCompressCodecsInfo exercises DLL-global SetCodecs.
#include "Common/MyInitGuid.h"

#include "7zip/Archive/IArchive.h"
#include "7zip/ICoder.h"
#include "7zip/PropID.h"
#include "Common/MyCom.h"
#include <atomic>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#ifndef _WIN32
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace {
void LocateFixture() {}

std::filesystem::path FixtureFolder() {
#ifdef _WIN32
    HMODULE fixture = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LocateFixture), &fixture))
        return {};
    wchar_t path[32768];
    const DWORD length = GetModuleFileNameW(fixture, path, 32768);
    if (!length || length == 32768)
        return {};
    return std::filesystem::path(std::wstring(path, length)).parent_path();
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void *>(&LocateFixture), &info))
        return {};
    return std::filesystem::path(info.dli_fname).parent_path();
#endif
}

void *CoreFunction(const char *name) {
    const auto folder = FixtureFolder();
    if (folder.empty())
        return nullptr;
#ifdef _WIN32
    const auto core = folder.parent_path() / "7z.dll";
    return reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(core.c_str()), name));
#else
    const auto path = folder.parent_path() / "7z.so";
    void *module = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!module)
        return nullptr;
    void *function = dlsym(module, name);
    dlclose(module); // CCodecs already owns the module; no extra global lease.
    return function;
#endif
}

HRESULT CoreProperty(PROPID property, PROPVARIANT *value) {
    const auto count =
        reinterpret_cast<Func_GetNumberOfFormats>(CoreFunction("GetNumberOfFormats"));
    const auto get =
        reinterpret_cast<Func_GetHandlerProperty2>(CoreFunction("GetHandlerProperty2"));
    UInt32 formats = 0;
    if (!count || !get || count(&formats) != S_OK)
        return E_FAIL;
    for (UInt32 i = 0; i < formats; ++i) {
        PROPVARIANT name{};
        const HRESULT result = get(i, NArchive::NHandlerPropID::kName, &name);
        const bool is7z =
            result == S_OK && name.vt == VT_BSTR && std::wcscmp(name.bstrVal, L"7z") == 0;
        if (name.vt == VT_BSTR)
            SysFreeString(name.bstrVal);
        if (is7z)
            return get(i, property, value);
    }
    return E_FAIL;
}

std::filesystem::path ControlPath(const char *name) {
    std::ifstream path(FixtureFolder() / "resources/fixture" / name, std::ios::binary);
    const std::string utf8(std::istreambuf_iterator<char>(path), {});
    return utf8.empty() ? std::filesystem::path() : std::filesystem::u8path(utf8);
}

UInt64 CurrentProcess() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

bool WriterEnabled() {
    const auto path = ControlPath("writer-enabled-path.txt");
    if (path.empty())
        return false;
    UInt64 process = 0;
    std::ifstream control(path);
    return bool(control >> process) && process == CurrentProcess();
}

class CFailedOutput final : public IOutStream, public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_2(ISequentialOutStream, IOutStream)
    CMyComPtr<IOutStream> output_;

  public:
    explicit CFailedOutput(ISequentialOutStream *output) {
        output->QueryInterface(IID_IOutStream, (void **)&output_);
    }
    Z7_COM7F_IMF(Write(const void *, UInt32, UInt32 *processed)) override {
        if (processed)
            *processed = 0;
        return E_FAIL;
    }
    Z7_COM7F_IMF(Seek(Int64 offset, UInt32 origin, UInt64 *position)) override {
        return output_ ? output_->Seek(offset, origin, position) : E_NOINTERFACE;
    }
    Z7_COM7F_IMF(SetSize(UInt64 size)) override {
        return output_ ? output_->SetSize(size) : E_NOINTERFACE;
    }
};

class CGlobalOnlyArchive final : public IInArchive,
                                 public IOutArchive,
                                 public ISetProperties,
                                 public CMyUnknownImp {
    Z7_COM_QI_BEGIN2(IInArchive)
    else if (writer_ && iid == IID_IOutArchive) {
        *outObject = static_cast<IOutArchive *>(this);
    }
    else if (properties_ && iid == IID_ISetProperties) {
        *outObject = static_cast<ISetProperties *>(this);
    }
    Z7_COM_QI_END
    Z7_COM_ADDREF_RELEASE
    CMyComPtr<IInArchive> archive_;
    CMyComPtr<IOutArchive> writer_;
    CMyComPtr<ISetProperties> properties_;
    std::filesystem::path propertyControl_;
    std::filesystem::path diskControl_;
    std::atomic<bool> diskSizeReported_{false};
    std::filesystem::path writerControl_;

  public:
    explicit CGlobalOnlyArchive(IInArchive *archive)
        : archive_(archive), propertyControl_(ControlPath("property-control-path.txt")),
          diskControl_(ControlPath("disk-control-path.txt")),
          writerControl_(ControlPath("writer-control-path.txt")) {
        if (WriterEnabled()) {
            archive_.QueryInterface(IID_IOutArchive, &writer_);
            archive_.QueryInterface(IID_ISetProperties, &properties_);
        }
    }
    Z7_COM7F_IMF(SetProperties(const wchar_t *const *names, const PROPVARIANT *values,
                               UInt32 count))
    override {
        return properties_ ? properties_->SetProperties(names, values, count) : E_NOINTERFACE;
    }
    Z7_COM7F_IMF(GetFileTimeType(UInt32 *type)) override {
        return writer_ ? writer_->GetFileTimeType(type) : E_NOINTERFACE;
    }
    Z7_COM7F_IMF(UpdateItems(ISequentialOutStream *output, UInt32 count,
                             IArchiveUpdateCallback *callback))
    override {
        if (!writer_)
            return E_NOINTERFACE;
        UInt64 process = 0;
        UInt32 requested = 0;
        std::ifstream control(writerControl_);
        const bool failOutput = (control >> process >> requested) && process == CurrentProcess() &&
                                requested == UInt32(E_FAIL);
        control.close();
        CMyComPtr<ISequentialOutStream> failed;
        if (failOutput)
            failed = new CFailedOutput(output);
        const HRESULT written =
            writer_->UpdateItems(failOutput ? failed.Interface() : output, count, callback);
        if (!writerControl_.empty()) {
            auto receipt = writerControl_;
            receipt += ".hit";
            std::ofstream(receipt, std::ios::app)
                << CurrentProcess() << ' ' << count << ' ' << UInt32(written) << '\n';
        }
        if ((written != S_OK && !(failOutput && written == E_FAIL)) || writerControl_.empty())
            return written;
        for (;;) {
            UInt64 process = 0;
            UInt32 result = 0, hold = 0;
            std::ifstream control(writerControl_);
            if (!(control >> process >> result >> hold) || process != CurrentProcess())
                return written;
            control.close();
            if (!hold && result != UInt32(E_ABORT))
                return HRESULT(result);
            const HRESULT progress = callback->SetCompleted(nullptr);
            if (progress != S_OK)
                return progress;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    Z7_COM7F_IMF(Open(IInStream *stream, const UInt64 *limit, IArchiveOpenCallback *callback))
    override {
        const HRESULT opened = archive_->Open(stream, limit, callback);
        if (opened != S_OK || propertyControl_.empty() || !callback)
            return opened;
        UInt32 actualCount = 0;
        if (archive_->GetNumberOfItems(&actualCount) != S_OK)
            return opened;
        bool observed = false;
        for (;;) {
            std::ifstream control(propertyControl_);
            UInt64 process = 0;
            UInt32 index = 0, result = 0, count = 0;
            if (!(control >> process >> index >> result >> count) || process != CurrentProcess() ||
                index != UInt32(-1) || count != actualCount ||
                (result != UInt32(E_ABORT) && result != UInt32(E_FAIL)))
                return opened;
            // The GUI releases this hold by removing its control file. Do not
            // retain a Windows CRT handle across a possibly paused callback.
            control.close();
            if (!observed) {
                auto receipt = propertyControl_;
                receipt += ".open";
                std::ofstream(receipt, std::ios::app) << process << ' ' << count << '\n';
                observed = true;
            }
            if (result == UInt32(E_FAIL))
                return E_FAIL;
            const HRESULT progress = callback->SetCompleted(nullptr, nullptr);
            if (progress != S_OK)
                return progress;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    Z7_COM7F_IMF(Close()) override { return archive_->Close(); }
    Z7_COM7F_IMF(GetNumberOfItems(UInt32 *count)) override {
        return archive_->GetNumberOfItems(count);
    }
    Z7_COM7F_IMF(GetProperty(UInt32 index, PROPID property, PROPVARIANT *value)) override {
        if (!propertyControl_.empty()) {
            std::ifstream control(propertyControl_);
            UInt64 faultProcess = 0;
            UInt32 faultIndex = 0, result = 0;
            if (control >> faultProcess >> faultIndex >> result &&
                faultProcess == CurrentProcess() && index == faultIndex) {
                UInt32 expectedCount = 0, actualCount = 0;
                control >> expectedCount; // Older controls omit the optional count filter.
                UInt32 requestedProperty = kpidAttrib;
                control >> requestedProperty;
                if (property != requestedProperty)
                    return archive_->GetProperty(index, property, value);
                if (expectedCount && (archive_->GetNumberOfItems(&actualCount) != S_OK ||
                                      actualCount != expectedCount))
                    return archive_->GetProperty(index, property, value);
                auto receipt = propertyControl_;
                receipt += ".hit";
                std::ofstream(receipt, std::ios::app) << index << ' ' << result << '\n';
                if (result == S_OK)
                    return archive_->GetProperty(index, property, value);
                *value = PROPVARIANT{};
                return static_cast<HRESULT>(static_cast<Int32>(result));
            }
        }
        if (property == kpidSize && index == 0 && !diskControl_.empty()) {
            std::ifstream control(diskControl_);
            UInt64 process = 0;
            UInt32 count = 0;
            if (control >> process && process == CurrentProcess() &&
                archive_->GetNumberOfItems(&count) == S_OK && count == 1 &&
                !diskSizeReported_.exchange(true)) {
                // Force only PanelItemOpen's route decision. Extraction must
                // retain the real size, including native Windows preallocation.
                auto receipt = diskControl_;
                receipt += ".hit";
                std::ofstream(receipt, std::ios::app) << index << ' ' << count << '\n';
                *value = PROPVARIANT{};
                value->vt = VT_UI8;
                value->uhVal.QuadPart = UInt64(1) << 63;
                return S_OK;
            }
        }
        return archive_->GetProperty(index, property, value);
    }
    Z7_COM7F_IMF(Extract(const UInt32 *indices, UInt32 count, Int32 test,
                         IArchiveExtractCallback *callback))
    override {
        if (!propertyControl_.empty()) {
            auto receipt = propertyControl_;
            receipt += ".extract";
            std::ofstream(receipt, std::ios::app) << CurrentProcess() << ' ' << count << '\n';
        }
        return archive_->Extract(indices, count, test, callback);
    }
    Z7_COM7F_IMF(GetArchiveProperty(PROPID property, PROPVARIANT *value)) override {
        return archive_->GetArchiveProperty(property, value);
    }
    Z7_COM7F_IMF(GetNumberOfProperties(UInt32 *count)) override {
        return archive_->GetNumberOfProperties(count);
    }
    Z7_COM7F_IMF(GetPropertyInfo(UInt32 index, BSTR *name, PROPID *property, VARTYPE *type))
    override { return archive_->GetPropertyInfo(index, name, property, type); }
    Z7_COM7F_IMF(GetNumberOfArchiveProperties(UInt32 *count)) override {
        return archive_->GetNumberOfArchiveProperties(count);
    }
    Z7_COM7F_IMF(GetArchivePropertyInfo(UInt32 index, BSTR *name, PROPID *property, VARTYPE *type))
    override { return archive_->GetArchivePropertyInfo(index, name, property, type); }
};
} // namespace

STDAPI GetModuleProp(PROPID property, PROPVARIANT *value) {
    const auto get = reinterpret_cast<Func_GetModuleProp>(CoreFunction("GetModuleProp"));
    return get ? get(property, value) : E_FAIL;
}
STDAPI GetNumberOfFormats(UInt32 *count) {
    *count = 1;
    return S_OK;
}
STDAPI GetHandlerProperty2(UInt32 index, PROPID property, PROPVARIANT *value) {
    if (index)
        return E_INVALIDARG;
    *value = PROPVARIANT{};
    if (property == NArchive::NHandlerPropID::kName ||
        property == NArchive::NHandlerPropID::kExtension) {
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocString(
            property == NArchive::NHandlerPropID::kName ? L"SevenzipFixture7z" : L"fixture7z");
        return value->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (property == NArchive::NHandlerPropID::kUpdate) {
        value->vt = VT_BOOL;
        value->boolVal = WriterEnabled() ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    return CoreProperty(property, value);
}
STDAPI SetCodecs(ICompressCodecsInfo *codecs) {
    const auto set = reinterpret_cast<Func_SetCodecs>(CoreFunction("SetCodecs"));
    return set ? set(codecs) : E_FAIL;
}
STDAPI CreateObject(const GUID *classID, const GUID *iid, void **object) {
    *object = nullptr;
    std::ifstream asset(FixtureFolder() / "resources/fixture/marker.txt", std::ios::binary);
    if (!asset || std::string(std::istreambuf_iterator<char>(asset), {}) != "fixture resource")
        return E_FAIL;
    if (*iid != IID_IInArchive && (*iid != IID_IOutArchive || !WriterEnabled()))
        return E_NOINTERFACE;
    const auto create = reinterpret_cast<Func_CreateObject>(CoreFunction("CreateObject"));
    if (!create)
        return E_FAIL;
    CMyComPtr<IInArchive> archive;
    const HRESULT result = create(classID, &IID_IInArchive, reinterpret_cast<void **>(&archive));
    if (result != S_OK)
        return result;
    CMyComPtr<IInArchive> facade = new CGlobalOnlyArchive(archive);
    return facade->QueryInterface(*iid, object);
}
