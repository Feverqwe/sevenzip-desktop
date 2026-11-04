#include "archive/password/archivepassword.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
void Erase(void *data, size_t size) noexcept {
    auto *bytes = static_cast<volatile unsigned char *>(data);
    while (size--)
        *bytes++ = 0;
}
} // namespace

NativePassword::NativePassword(const UString &value) { UString::operator=(value); }
NativePassword::NativePassword(const NativePassword &value)
    : NativePassword(static_cast<const UString &>(value)) {}
NativePassword::NativePassword(const QString &value) { *this = value; }
NativePassword::~NativePassword() { Empty(); }
void WipeNativePassword(UString &value) noexcept {
    Erase(value.Ptr_non_const(), size_t(value.Len()) * sizeof(wchar_t));
    // Retain upstream's full-capacity wipe as well, including truncated tails.
    value.Wipe_and_Empty();
    std::atomic_signal_fence(std::memory_order_seq_cst);
}
void NativePassword::Empty() { WipeNativePassword(*this); }
NativePassword &NativePassword::operator=(const NativePassword &value) {
    return *this = static_cast<const UString &>(value);
}
NativePassword &NativePassword::operator=(const UString &value) {
    if (this != &value) {
        Empty();
        UString::operator=(value);
    }
    return *this;
}
NativePassword &NativePassword::operator=(const wchar_t *value) {
    // A caller may pass a pointer into this buffer.
    NativePassword copy;
    copy.UString::operator=(value);
    return *this = copy;
}
NativePassword &NativePassword::operator=(const char *value) {
    Empty();
    UString::operator=(value);
    return *this;
}
NativePassword &NativePassword::operator=(const QString &value) {
    Empty();
    if (value.size() > (std::numeric_limits<unsigned>::max)())
        throw std::bad_alloc();
    const auto length = value.toWCharArray(GetBuf(unsigned(value.size())));
    ReleaseBuf_SetEnd(unsigned(length));
    return *this;
}

struct ArchivePassword::Storage {
    wchar_t *Data = nullptr;
    size_t Bytes = 0;
    unsigned Length = 0;
    bool Locked = false;

    explicit Storage(size_t capacity) {
        if (capacity > ((std::numeric_limits<size_t>::max)() / sizeof(wchar_t)) - 1)
            throw std::bad_alloc();
        const size_t requested = (capacity + 1) * sizeof(wchar_t);
#ifdef _WIN32
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        const size_t page = info.dwPageSize;
#else
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0)
            throw std::bad_alloc();
        const size_t page = size_t(pageSize);
#endif
        if (requested > (std::numeric_limits<size_t>::max)() - (page - 1))
            throw std::bad_alloc();
        Bytes = ((requested + page - 1) / page) * page;
#ifdef _WIN32
        Data = static_cast<wchar_t *>(
            VirtualAlloc(nullptr, Bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!Data)
            throw std::bad_alloc();
        Locked = VirtualLock(Data, Bytes) != 0;
#else
        void *mapping =
            mmap(nullptr, Bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED)
            throw std::bad_alloc();
        Data = static_cast<wchar_t *>(mapping);
        Locked = mlock(Data, Bytes) == 0;
#ifdef MADV_DONTDUMP
        madvise(Data, Bytes, MADV_DONTDUMP);
#endif
#endif
    }
    ~Storage() {
        Erase(Data, Bytes);
#ifdef _WIN32
        if (Locked)
            VirtualUnlock(Data, Bytes);
        VirtualFree(Data, 0, MEM_RELEASE);
#else
        if (Locked)
            munlock(Data, Bytes);
        munmap(Data, Bytes);
#endif
    }
};

ArchivePassword::ArchivePassword(const QString &value) {
    if (value.isEmpty())
        return;
    if (value.size() > (std::numeric_limits<unsigned>::max)())
        throw std::bad_alloc();
    auto storage = std::make_shared<Storage>(size_t(value.size()));
    storage->Length = unsigned(value.toWCharArray(storage->Data));
    storage_ = std::move(storage);
}
ArchivePassword::ArchivePassword(const UString &value)
    : ArchivePassword(value.Ptr(), value.Len()) {}
ArchivePassword::ArchivePassword(const wchar_t *value, unsigned length) {
    if (!length)
        return;
    auto storage = std::make_shared<Storage>(length);
    storage->Length = length;
    std::memcpy(storage->Data, value, size_t(length) * sizeof(wchar_t));
    storage_ = std::move(storage);
}
bool ArchivePassword::isEmpty() const { return !storage_; }
void ArchivePassword::clear() { storage_.reset(); }
QString ArchivePassword::toQString() const {
    return storage_ ? QString::fromWCharArray(storage_->Data, storage_->Length) : QString();
}
NativePassword ArchivePassword::native() const {
    NativePassword value;
    if (storage_)
        value.SetFrom(storage_->Data, storage_->Length);
    return value;
}
HRESULT ArchivePassword::copyToBstr(BSTR *value) const {
    if (!value)
        return E_INVALIDARG;
    *value = SysAllocStringLen(storage_ ? storage_->Data : L"", storage_ ? storage_->Length : 0);
    return *value ? S_OK : E_OUTOFMEMORY;
}
