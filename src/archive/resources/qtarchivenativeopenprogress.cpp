#include "archive/resources/qtarchivenativeopenprogress.h"

HRESULT CNativeOpenProgress::ResetArchivePassword() {
    Password.clear();
    auto *retry = dynamic_cast<IArchivePasswordRetryUI *>(_source);
    return retry ? retry->ResetArchivePassword() : E_NOTIMPL;
}

HRESULT CNativeOpenProgress::Open_CheckBreak() {
    if (!_source && _open)
        return _open->SetCompleted(nullptr, nullptr);
    return _source ? _source->Open_CheckBreak()
                   : (_progress ? _progress->SetCompleted(nullptr) : S_OK);
}
HRESULT CNativeOpenProgress::Open_SetTotal(const UInt64 *files, const UInt64 *bytes) {
    if (!_source && _open)
        return _open->SetTotal(files, bytes);
    return _source ? _source->Open_SetTotal(files, bytes)
                   : (_progress && bytes ? _progress->SetTotal(*bytes) : Open_CheckBreak());
}
HRESULT CNativeOpenProgress::Open_SetCompleted(const UInt64 *files, const UInt64 *bytes) {
    if (!_source && _open)
        return _open->SetCompleted(files, bytes);
    return _source ? _source->Open_SetCompleted(files, bytes)
                   : (_progress ? _progress->SetCompleted(bytes) : S_OK);
}
HRESULT CNativeOpenProgress::Open_Finished() { return Open_CheckBreak(); }
HRESULT CNativeOpenProgress::Open_FscryptKeys(BSTR *keys) {
    HRESULT result;
    if (auto *request = dynamic_cast<IFscryptKeyUI *>(_source))
        result = request->Open_FscryptKeys(keys);
    else {
        CMyComPtr<IFscryptKeyRequest> cryptoRequest;
        if (_open)
            _open->QueryInterface(IID_IFscryptKeyRequest, (void **)&cryptoRequest);
        else if (_progress)
            _progress->QueryInterface(IID_IFscryptKeyRequest, (void **)&cryptoRequest);
        result =
            cryptoRequest ? cryptoRequest->GetFscryptKeys(keys) : Open_CryptoGetTextPassword(keys);
    }
    if (result == S_OK)
        Password = ArchivePassword(*keys, SysStringLen(*keys));
    return result;
}
HRESULT CNativeOpenProgress::Open_CryptoGetTextPassword(BSTR *password) {
    HRESULT result;
    if (_source)
        result = _source->Open_CryptoGetTextPassword(password);
    else {
        CMyComPtr<ICryptoGetTextPassword> crypto;
        if (_open)
            _open->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto);
        else if (_progress)
            _progress->QueryInterface(IID_ICryptoGetTextPassword, (void **)&crypto);
        result = crypto ? crypto->CryptoGetTextPassword(password) : E_NOINTERFACE;
    }
    if (result == S_OK)
        Password = ArchivePassword(*password, SysStringLen(*password));
    return result;
}
