#include "filemanager/App.h"
#include "filemanager/panels/qtpaneldragnative.h"
#include "filemanager/providers/FSFolder.h"
#include "platform/shell/Shell.h"
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>

namespace {
FORMATETC Format(UINT id) { return {CLIPFORMAT(id), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; }
QByteArray Bytes(IDataObject *data, FORMATETC format) {
    STGMEDIUM medium{};
    if (data->GetData(&format, &medium) != S_OK)
        return {};
    const void *bytes = GlobalLock(medium.hGlobal);
    QByteArray result;
    if (bytes) {
        result =
            QByteArray(static_cast<const char *>(bytes), qsizetype(GlobalSize(medium.hGlobal)));
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return result;
}
HRESULT SetBytes(IDataObject *data, FORMATETC format, const QByteArray &bytes, BOOL release) {
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = GlobalAlloc(GHND, SIZE_T(bytes.size()));
    void *dest = GlobalLock(medium.hGlobal);
    if (!dest)
        return E_OUTOFMEMORY;
    std::memcpy(dest, bytes.constData(), size_t(bytes.size()));
    GlobalUnlock(medium.hGlobal);
    const HRESULT result = data->SetData(&format, &medium, release);
    if (!release || result != S_OK)
        ReleaseStgMedium(&medium);
    return result;
}
class Target final : public IQtDropTargetPresentation {
  public:
    int PanelAtPoint(int, int) const override { return 1; }
    bool PanelEnabled(unsigned) const override { return true; }
    int FolderAtPoint(unsigned, int, int, UString &) const override { return -1; }
    void Highlight(unsigned, int, bool) override {}
    bool IsSameDrive(const UString &, const UStringVector &) const override { return true; }
    UInt32 Drag_OnContextMenu(int, int, UInt32, DWORD) override { return NDragMenu::k_Cancel; }
};
class DataTap final : public IDataObject {
    CMyComPtr<IDataObject> source_;

  public:
    bool performedWasSet = false, performedOwnership = false;
    DWORD performed = DWORD(-1);
    explicit DataTap(IDataObject *source) : source_(source) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f, STGMEDIUM *m) override {
        return source_->GetData(f, m);
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *f, STGMEDIUM *m) override {
        return source_->GetDataHere(f, m);
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override {
        return source_->QueryGetData(f);
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *f, FORMATETC *out) override {
        return source_->GetCanonicalFormatEtc(f, out);
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *f, STGMEDIUM *m, BOOL release) override {
        if (f->cfFormat == RegisterClipboardFormatW(CFSTR_PERFORMEDDROPEFFECT) &&
            m->tymed == TYMED_HGLOBAL) {
            const auto *value = static_cast<const DWORD *>(GlobalLock(m->hGlobal));
            if (value && GlobalSize(m->hGlobal) >= sizeof(DWORD)) {
                performed = *value;
                performedWasSet = true;
                performedOwnership = release;
            }
            if (value)
                GlobalUnlock(m->hGlobal);
        }
        return source_->SetData(f, m, release);
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC **out) override {
        return source_->EnumFormatEtc(direction, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *f, DWORD flags, IAdviseSink *sink,
                                      DWORD *out) override {
        return source_->DAdvise(f, flags, sink, out);
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD connection) override {
        return source_->DUnadvise(connection);
    }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **out) override {
        return source_->EnumDAdvise(out);
    }
};
class NativeRecipient final : public IQtPanelDragServices {
    CApp &app_;

  public:
    bool move = false, cancel = false;
    bool performedWasSet = false, performedOwnership = false;
    DWORD performed = DWORD(-1);
    HRESULT entered = E_FAIL, dropped = E_FAIL;
    UInt32 finalFlags = 0;
    CMyComPtr<IDataObject> retained;
    std::weak_ptr<CDataObject> sourceOwner;
    explicit NativeRecipient(CApp &app) : app_(app) {}
    HRESULT DoDragDrop(CDataObject &data, CDropSource &source, DWORD allowed,
                       DWORD &effect) override {
        RINOK(CreatePanelDragDataObject(data.shared_from_this(), &retained))
        sourceOwner = data.shared_from_this();
        DataTap tap(retained);
        CDataObject external;
        external.NativeData = ReadPanelDragDataObject(&tap);
        const DWORD keys = move ? MK_SHIFT : MK_CONTROL;
        effect = allowed;
        entered = app_.DropTarget().DragEnter(&external, keys, 0, 0, &effect);
        if (entered != S_OK)
            return entered;
        source.GiveFeedback(effect);
        const HRESULT result = source.QueryContinueDrag(cancel, 0);
        if (result == DRAGDROP_S_CANCEL) {
            app_.DropTarget().DragLeave();
            return result;
        }
        effect = allowed;
        dropped = app_.DropTarget().Drop(&external, keys, 0, 0, &effect);
        performedWasSet = tap.performedWasSet;
        performedOwnership = tap.performedOwnership;
        performed = tap.performed;
        finalFlags = data.m_Transfer.Target.Flags;
        return result;
    }
};
} // namespace

class NativeDragProtocolTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeTargetDelegatesRealPanelCopyMoveAndCancel_data() {
        QTest::addColumn<bool>("move");
        QTest::addColumn<bool>("cancel");
        QTest::addColumn<bool>("menuCancel");
        QTest::newRow("copy") << false << false << false;
        QTest::newRow("move") << true << false << false;
        QTest::newRow("cancel-copy") << false << true << false;
        QTest::newRow("cancel-move") << true << true << false;
        QTest::newRow("native-menu-cancel") << false << false << true;
    }
    void nativeTargetDelegatesRealPanelCopyMoveAndCancel() {
        QFETCH(bool, move);
        QFETCH(bool, cancel);
        QFETCH(bool, menuCancel);
        QTemporaryDir sourceRoot, targetRoot;
        QVERIFY(sourceRoot.isValid() && targetRoot.isValid());
        const QString name = QStringLiteral("native перенос δ with space.txt");
        QFile file(sourceRoot.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("native panel bytes"), qint64(18));
        file.close();
        Target presentation;
        CApp app;
        for (unsigned pane = 0; pane < 2; ++pane) {
            app.Panels[pane].SetNewFolder(
                new NFsFolder::CFSFolder(pane ? targetRoot.path() : sourceRoot.path()));
            app.Panels[pane].LoadFullPath();
        }
        QCOMPARE(app.Panels[0].GetFolder()->LoadItems(), S_OK);
        app.m_PanelCallbackImp[0].Init(&app, 0);
        app.Panels[0].AttachPanelCallback(&app.m_PanelCallbackImp[0]);
        app.DropTarget().AttachPresentation(&presentation);
        QTableWidget list(1, 1);
        list.setSelectionBehavior(QAbstractItemView::SelectRows);
        auto *row = new QTableWidgetItem(name);
        row->setData(Qt::UserRole + 6, 0u);
        list.setItem(0, 0, row);
        list.setCurrentCell(0, 0);
        app.Panels[0].AttachSelectionView(&list);
        NativeRecipient receiver(app);
        receiver.move = move;
        receiver.cancel = cancel;
        app.Panels[0].AttachDragServices(&receiver);
        app.Panels[0].OnDrag(menuCancel);
        QCOMPARE(receiver.entered, S_OK);
        QCOMPARE(row->isSelected(), cancel || menuCancel);
        QCOMPARE(app.DropTarget().SrcPanelIndex, -1);
        QVERIFY(receiver.sourceOwner.lock());
        QVERIFY(!receiver.sourceOwner.lock()->Panel);
        if (!cancel && !menuCancel) {
            QCOMPARE(receiver.dropped, S_OK);
            QVERIFY(receiver.finalFlags & k_TargetFlags_MustBeProcessedBySource);
            QFile copied(targetRoot.filePath(name));
            QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(), QByteArray("native panel bytes"));
        } else {
            QVERIFY(!QFileInfo::exists(targetRoot.filePath(name)));
        }
        QCOMPARE(QFileInfo::exists(file.fileName()), cancel || menuCancel || !move);
        QCOMPARE(receiver.performedWasSet, !cancel);
        if (!cancel) {
            QVERIFY(receiver.performedOwnership);
            QCOMPARE(receiver.performed, DWORD(menuCancel ? DROPEFFECT_NONE : DROPEFFECT_COPY));
        }
        // The real source frame has returned. The receiver still owns IDataObject.
        UStringVector names;
        QCOMPARE(ReadPanelDragDataObject(receiver.retained)->GetNames(names), S_OK);
        QCOMPARE(names.Size(), unsigned(1));
        QCOMPARE(QDir::fromNativeSeparators(ToQString(names[0])), file.fileName());
    }
    void sourceWireValidationAndMediumOwnership() {
        auto model = std::make_shared<CDataObject>();
        model->Names.Add(L"C:\\файл δ with spaces.txt");
        CMyComPtr<IDataObject> data;
        QCOMPARE(CreatePanelDragDataObject(model, &data), S_OK);
        auto transfer = Format(RegisterClipboardFormatW(L"7-Zip::GetTransfer"));
        const QByteArray wire = Bytes(data, transfer);
        QCOMPARE(wire.size(), qsizetype(112));
        UInt32 words[28]{};
        std::memcpy(words, wire.constData(), sizeof(words));
        QCOMPARE(words[0], UInt32(3));
        QCOMPARE(words[1], UInt32(112));
        QCOMPARE(words[2], UInt32(1));
        QCOMPARE(words[8], UInt32(GetCurrentProcessId()));
        QCOMPARE(words[16], UInt32(k_SourceFlags_LeftButton | k_SourceFlags_DoNotWaitFinish));
        for (int i = 9; i < 16; ++i)
            QCOMPARE(words[i], UInt32(0));
        for (int i = 17; i < 28; ++i)
            QCOMPARE(words[i], UInt32(0));
        transfer.tymed = TYMED_ISTREAM;
        QCOMPARE(data->QueryGetData(&transfer), DV_E_TYMED);
        transfer.tymed = TYMED_HGLOBAL;
        transfer.dwAspect = DVASPECT_ICON;
        QCOMPARE(data->QueryGetData(&transfer), DV_E_DVASPECT);
        auto set = Format(RegisterClipboardFormatW(L"7-Zip::SetTransfer"));
        QCOMPARE(SetBytes(data, set, QByteArray(4, '\0'), FALSE), E_FAIL);
        QByteArray malformed(112, '\0');
        QCOMPARE(SetBytes(data, set, malformed, FALSE), E_FAIL);
        QVERIFY(!model->m_Transfer_WasSet);
        // TRUE consumes even unsupported formats, matching the source's Explorer contract.
        set.cfFormat = CLIPFORMAT(RegisterClipboardFormatW(L"Sevenzip test unsupported"));
        QCOMPARE(SetBytes(data, set, QByteArray(4, 'x'), FALSE), DV_E_FORMATETC);
        QCOMPARE(SetBytes(data, set, QByteArray(4, 'x'), TRUE), S_OK);
        CMyComPtr<IEnumFORMATETC> enumerator;
        QCOMPARE(data->EnumFormatEtc(DATADIR_GET, &enumerator), S_OK);
        FORMATETC first{};
        ULONG fetched = 0;
        QCOMPARE(enumerator->Next(1, &first, &fetched), S_OK);
        QCOMPARE(first.cfFormat, CLIPFORMAT(CF_HDROP));
        QCOMPARE(fetched, ULONG(1));
        CMyComPtr<IEnumFORMATETC> clone;
        QCOMPARE(enumerator->Clone(&clone), S_OK);
        QCOMPARE(clone->Next(1, &first, &fetched), S_FALSE);
        QCOMPARE(enumerator->Reset(), S_OK);
        QCOMPARE(enumerator->Next(1, &first, &fetched), S_OK);
        // Each GetData returns independent storage; receiver mutation cannot alter source.
        const QByteArray original = Bytes(data, first);
        STGMEDIUM medium{};
        QCOMPARE(data->GetData(&first, &medium), S_OK);
        auto *bytes = static_cast<char *>(GlobalLock(medium.hGlobal));
        QVERIFY(bytes);
        std::memset(bytes, 0, GlobalSize(medium.hGlobal));
        GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        QCOMPARE(Bytes(data, first), original);
    }
    void nativeMetadataAndRetainedSourceLifetime() {
        auto model = std::make_shared<CDataObject>();
        model->Names.Add(L"C:\\retained δ.txt");
        std::weak_ptr<CDataObject> weak = model;
        CMyComPtr<IDataObject> data;
        QCOMPARE(CreatePanelDragDataObject(model, &data), S_OK);
        auto receiver = ReadPanelDragDataObject(data);
        QVERIFY(receiver->SetTargetFolder(L"C:\\destination δ\\"));
        CTargetTransferInfo info;
        info.FuncType = k_DragTargetMode_Drop_End;
        info.Flags = k_TargetFlags_MustBeProcessedBySource | k_TargetFlags_MenuWasShown;
        info.Cmd_Type = NDragMenu::k_Copy_Base;
        info.Cmd_Effect = DROPEFFECT_MOVE;
        info.KeyState = MK_SHIFT;
        info.OkEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE;
        info.X = -12;
        info.Y = 35;
        info.FolderType = k_FolderType_AltStreams;
        QVERIFY(receiver->SetTransfer(info));
        QCOMPARE(model->m_Transfer.Target.Flags, info.Flags);
        QCOMPARE(model->m_Transfer.Target.X, info.X);
        QCOMPARE(model->m_Transfer.Target.Y, info.Y);
        QCOMPARE(model->m_Transfer.Target.Cmd_Effect, info.Cmd_Effect);
        QCOMPARE(model->m_Transfer.Target.FolderType, info.FolderType);
        UInt32 flags = 0;
        QVERIFY(receiver->ReadTransferFlags(flags));
        QVERIFY(flags & k_SourceFlags_DoNotProcessInTarget);
        QVERIFY(flags & k_SourceFlags_SetTargetFolder_NonEmpty);
        info.FuncType = k_DragTargetMode_Leave;
        QVERIFY(receiver->SetTransfer(info));
        QVERIFY(!model->m_Transfer_WasSet);
        model.reset();
        data.Release();
        QVERIFY(!weak.expired());
        UStringVector names;
        QCOMPARE(receiver->GetNames(names), S_OK);
        QCOMPARE(names.Size(), unsigned(1));
        receiver.reset();
        QVERIFY(weak.expired());
    }
    void lateUnpreparedSourceCannotCallClosedPanel() {
        auto model = std::make_shared<CDataObject>();
        model->NeedCall_Copy = true;
        model->Names.Add(L"C:\\unprepared δ.txt");
        CMyComPtr<IDataObject> data;
        QCOMPARE(CreatePanelDragDataObject(model, &data), S_OK);
        STGMEDIUM medium{};
        auto unsupported = Format(CF_UNICODETEXT);
        QCOMPARE(data->GetData(&unsupported, &medium), DV_E_FORMATETC);
        QCOMPARE(model->Copy_HRESULT, S_OK);
        QCOMPARE(medium.tymed, DWORD(TYMED_NULL));
        auto format = Format(CF_HDROP);
        QCOMPARE(data->GetData(&format, &medium), E_ABORT);
        QCOMPARE(medium.tymed, DWORD(TYMED_NULL));
        QCOMPARE(model->Copy_HRESULT, E_ABORT);
        QVERIFY(!model->Copy_WasCalled);
    }
};
QTEST_MAIN(NativeDragProtocolTests)
#include "native_drag_protocol_tests.moc"
