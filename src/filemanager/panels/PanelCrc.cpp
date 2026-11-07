// PanelCrc.cpp

// Adapted from 7-Zip 0766b733fe3e06dd2a7f9a3cfbf2108ac73abd17.
// Igor Pavlov, LGPL-2.1-or-later; see third_party/7zip/License.txt.

#include "archive/hashing/HashCalc.h"
#include "archive/hashing/qtfilecrc.h"
#include "filemanager/App.h"
#include "filemanager/properties/qtpropertyreport.h"

HRESULT CApp::CalculateCrc2(const UString &methodName, const CExternalCodecs *externalCodecs) {
    unsigned srcPanelIndex = GetFocusedPanelIndex();
    CPanel &srcPanel = Panels[srcPanelIndex];
    CRecordVector<UInt32> indices;
    srcPanel.Get_ItemIndices_OperSmart(indices);
    if (indices.IsEmpty())
        return S_OK;
    std::function<bool(const CHashBundle &)> present;
    if ((srcPanel.IsFSFolder() || srcPanel.IsArcFolder()) && indices.Size() == 1 &&
        !srcPanel.IsItem_Folder(indices[0])) {
        QtPropertyReport properties;
        srcPanel.Properties(indices, properties);
        present = [&srcPanel, properties = std::move(properties)](const CHashBundle &hash) mutable {
            if (hash.NumErrors != 0 || hash.NumFiles != 1 || hash.NumDirs != 0)
                return false;
            properties.BeginGroup(QtPropertyReport::Scope::Checksums);
            FOR_VECTOR(i, hash.Hashers) {
                const auto &hasher = hash.Hashers[i];
                char digest[(k_HashCalc_DigestSize_Max + k_HashCalc_ExtraSize) * 2 + 16];
                hasher.WriteToString(k_HashCalc_Index_DataSum, digest);
                properties.AddValue(UString(hasher.Name), UString(digest));
            }
            srcPanel.ShowProperties(std::move(properties));
            return true;
        };
    }
    if (!srcPanel.Is_IO_FS_Folder()) {
        CCopyToOptions options;
        options.streamMode = true;
        options.showErrorMessages = true;
        options.hashMethods.Add(methodName);
        options.ExternalCodecs = externalCodecs;
        options.PresentHashResult = std::move(present);
        options.NeedRegistryZone = false;
        UStringVector messages;
        return srcPanel.CopyTo(options, indices, &messages);
    }
    UStringVector paths;
    FOR_VECTOR(i, indices)
    paths.Add(srcPanel.GetItemRelPath(indices[i]));
    const HRESULT result = CalculateFileCrc(methodName, externalCodecs, srcPanel.GetFsPath(), paths,
                                            !srcPanel.GetFlatMode(), _window, nullptr, present);

    RefreshTitle(true);
    // The original progress/results windows already report partial hash errors.
    return result == S_FALSE ? S_OK : result;
}

void CApp::CalculateCrc(const char *methodName) {
    HRESULT res = CalculateCrc2(UString(methodName));
    if (res != S_OK && res != E_ABORT) {
        unsigned srcPanelIndex = GetFocusedPanelIndex();
        CPanel &srcPanel = Panels[srcPanelIndex];
        srcPanel.MessageBox_Error_HRESULT(res);
    }
}
