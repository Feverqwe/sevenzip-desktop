// UpdateCallback100.h
// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#ifndef ZIP7_INC_UPDATE_CALLBACK100_H
#define ZIP7_INC_UPDATE_CALLBACK100_H

#include "Common/MyCom.h"

#include "7zip/ICoder.h"
#include "7zip/IPassword.h"

#include "archive/agent/IFolderArchive.h"
#include "archive/resources/qtarchiveupdatepublication.h"

#include "gui/UpdateCallbackGUI2.h"

#include "ui/progress/ProgressDialog2.h"

class CUpdateCallback100Imp Z7_final : public IFolderArchiveUpdateCallback,
                                       public IFolderArchiveUpdateCallback2,
                                       public IFolderArchiveUpdateCallback_MoveArc,
                                       public IFolderScanProgress,
                                       public ICryptoGetTextPassword2,
                                       public ICryptoGetTextPassword,
                                       public IArchiveOpenCallback,
                                       public ICompressProgressInfo,
                                       public CUpdateCallbackGUI2,
                                       public QtArchiveUpdatePublication,
                                       public CMyUnknownImp {
    Z7_COM_UNKNOWN_IMP_8(IFolderArchiveUpdateCallback, IFolderArchiveUpdateCallback2,
                         IFolderArchiveUpdateCallback_MoveArc, IFolderScanProgress,
                         ICryptoGetTextPassword2, ICryptoGetTextPassword, IArchiveOpenCallback,
                         ICompressProgressInfo)

    Z7_IFACE_COM7_IMP(IProgress)
    Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback)
    Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback2)
    Z7_IFACE_COM7_IMP(IFolderArchiveUpdateCallback_MoveArc)
    Z7_IFACE_COM7_IMP(IFolderScanProgress)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword2)
    Z7_IFACE_COM7_IMP(ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IArchiveOpenCallback)
    Z7_IFACE_COM7_IMP(ICompressProgressInfo)
};

#endif
