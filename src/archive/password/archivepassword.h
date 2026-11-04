#pragma once
#include "Common/MyString.h"
#include "Common/MyWindows.h"
#include <QString>
#include <memory>

// Also usable at mutable UString output boundaries before replacing a secret.
void WipeNativePassword(UString &value) noexcept;

// Short-lived adapter for APIs that require a mutable upstream UString.
// Do not retain a plain UString copy of this value.
class NativePassword : public UString_Wipe {
  public:
    NativePassword() = default;
    NativePassword(const UString &value);
    NativePassword(const NativePassword &value);
    explicit NativePassword(const QString &value);
    ~NativePassword();
    NativePassword &operator=(const NativePassword &value);
    NativePassword &operator=(const UString &value);
    NativePassword &operator=(const wchar_t *value);
    NativePassword &operator=(const char *value);
    NativePassword &operator=(const QString &value);
    void Empty();
};

// Immutable shared secret: copies retain the same allocation. The last owner
// wipes the entire mapping before releasing it. OS memory locking is best effort.
class ArchivePassword {
  public:
    ArchivePassword() = default;
    ArchivePassword(const QString &value);
    ArchivePassword(const UString &value);
    ArchivePassword(const wchar_t *value, unsigned length);
    bool isEmpty() const;
    void clear();
    QString toQString() const;
    NativePassword native() const;
    HRESULT copyToBstr(BSTR *value) const;

  private:
    struct Storage;
    std::shared_ptr<const Storage> storage_;
};
