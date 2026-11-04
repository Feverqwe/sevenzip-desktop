#pragma once
#include "7zip/Archive/IArchive.h"

// Optional native filesystem service. Item indexes are stable for the lifetime
// of the opening; enumeration appends items and never changes existing indexes.
inline constexpr GUID IID_IArchiveDirectoryAccess = {
    0x55c3e715, 0x6a82, 0x4c11, {0xb8, 0x9a, 0x67, 0xd4, 0x0f, 0x82, 0xae, 0x31}};
#define Z7_IFACEM_IArchiveDirectoryAccess(x)                                                       \
    x(SetDirectoryMode(BoolInt enabled))                                                           \
        x(LoadDirectory(UInt32 parent, BoolInt recursive, IProgress *progress))                    \
            x(IsDirectoryLoaded(UInt32 parent, BoolInt recursive, BoolInt *loaded))                \
                x(GetDirectoryItems(UInt32 parent, UInt32 *count, UInt32 *items))                  \
                    x(GetDirectoryInputStream(IInStream **stream))
struct Z7_DECLSPEC_NOVTABLE IArchiveDirectoryAccess : public IUnknown {
    Z7_COM7F_PURE(SetDirectoryMode(BoolInt enabled))
    // UINT32_MAX identifies the archive root. Recursive enumeration is explicit.
    Z7_COM7F_PURE(LoadDirectory(UInt32 parent, BoolInt recursive, IProgress *progress))
    Z7_COM7F_PURE(IsDirectoryLoaded(UInt32 parent, BoolInt recursive, BoolInt *loaded))
    // Query the required count with items=nullptr, then supply that capacity.
    Z7_COM7F_PURE(GetDirectoryItems(UInt32 parent, UInt32 *count, UInt32 *items))
    // Retained decoded input; consumers serialize its cursor through the
    // owning native generation, rather than exposing it directly to a worker.
    Z7_COM7F_PURE(GetDirectoryInputStream(IInStream **stream))
};
