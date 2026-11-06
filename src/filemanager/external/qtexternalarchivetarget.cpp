#include "filemanager/external/qtexternalarchivetarget.h"
#include "Common/StringConvert.h"
#include "archive/agent/Agent.h"
#include "archive/backend/archivebackend.h"
#include "archive/codecs/archivecodeccatalog.h"
#include "archive/resources/archivenativeopen.h"
#include "archive/resources/qtarchivedirectoryread.h"
#include "archive/resources/qtarchivefileresource.h"
#include "archive/resources/qtarchivefoldermanager.h"
#include "filemanager/external/qtexternalfilerecord.h"
#include "filemanager/panels/Panel.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QScopeGuard>
#include <limits>

namespace {
QtArchiveFileResource *Resource(IFolderFolder *folder) {
    const auto *native = dynamic_cast<CAgentFolder *>(folder);
    return native ? dynamic_cast<QtArchiveFileResource *>(native->GetEngineResource()) : nullptr;
}
HRESULT Prepare(CPanel &panel, QWidget *parent, const QString &settings) {
    auto *native = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    if (!native)
        return E_NOTIMPL;
    RINOK(ReadArchiveDirectory(*native, true, parent, settings))
    if (!panel.GetFlatMode())
        panel.ChangeFlatMode();
    return panel.LoadListItems();
}
HRESULT FindEntry(CPanel &panel, UInt32 rawIndex, const UString &path, UInt32 &found) {
    auto *native = dynamic_cast<CAgentFolder *>(panel.GetFolder());
    if (!native)
        return E_NOTIMPL;
    for (UInt32 index = 0; index < panel.GetLoadedItemCount(); ++index) {
        CUIntVector raw;
        native->GetRealIndices(&index, 1, false, false, raw);
        if (raw.Size() != 1 || raw[0] != rawIndex)
            continue;
        UString actual;
        RINOK(native->GetArchiveItemPath(raw[0], actual))
        if (actual != path || panel.IsItem_Folder(index))
            return E_INVALIDARG;
        found = index;
        return S_OK;
    }
    return E_INVALIDARG;
}
} // namespace

std::optional<QtExternalArchiveTarget>
QtExternalArchiveTarget::Capture(CPanel &source, const CTempFileInfo &file,
                                 const ArchiveBackend &backend, const QString &settingsPath) {
    const auto &parents = source.GetParentFolders();
    if (parents.IsEmpty() || source.IsThereReadOnlyFolder())
        return {};
    QtExternalArchiveTarget target;
    target.physical_ = ToQString(parents.Front().VirtualPath);
    target.settings_ = settingsPath;
    target.backendFactory_ = backend.independentBackendFactory();
    for (unsigned level = 0; level < parents.Size(); ++level) {
        IFolderFolder *folder = level + 1 < parents.Size()
                                    ? parents[level + 1].ParentFolder.Interface()
                                    : source.GetFolder();
        auto *native = dynamic_cast<CAgentFolder *>(folder);
        auto *resource = Resource(folder);
        if (!native || !resource)
            return {};
        const auto reader = resource->NativeReaderLease();
        if (!reader)
            return {};
        if (level == 0) {
            target.source_ = reader->SourceSnapshot();
            target.version_ = reader->codecCatalog()->version();
            if (!target.source_)
                return {};
        }
        const UInt32 index =
            level + 1 < parents.Size() ? parents[level + 1].FileIndex : file.FileIndex;
        CUIntVector raw;
        native->GetRealIndices(&index, 1, false, false, raw);
        if (raw.Size() != 1)
            return {};
        UString path;
        if (native->GetArchiveItemPath(raw[0], path) != S_OK)
            return {};
        target.layers_.push_back({{raw[0], path},
                                  reader->resolvedOpenType(),
                                  parents[level].UsePassword,
                                  parents[level].Password});
    }
    return target;
}

QJsonObject QtExternalArchiveTarget::Save() const {
    if (!source_)
        return {};
    QJsonArray layers;
    for (const auto &layer : layers_)
        layers.append(QJsonObject{{"index", qint64(layer.entry.index)},
                                  {"path", ToQString(layer.entry.path)},
                                  {"format", layer.format}});
    return {{"version", 1},
            {"codecs", int(version_)},
            {"physical", physical_},
            {"source", source_->Save()},
            {"layers", layers}};
}

QString QtExternalArchiveTarget::description() const {
    QStringList path{physical_};
    for (const auto &layer : layers_)
        path.append(ToQString(layer.entry.path));
    return path.join(QStringLiteral(" / "));
}

std::optional<QtExternalArchiveTarget>
QtExternalArchiveTarget::Restore(const QJsonObject &value, const ArchiveBackend &backend,
                                 const QString &settingsPath) {
    QtExternalArchiveTarget target;
    target.physical_ = value.value("physical").toString();
    target.source_ = ArchiveSourceSnapshot::Restore(value.value("source").toObject());
    const auto codecs = value.value("codecs");
    const qint64 version = codecs.toInteger(-1);
    const auto layers = value.value("layers").toArray();
    if (value.value("version").toInt() != 1 || !target.source_ || !codecs.isDouble() ||
        version <= 0 || version > std::numeric_limits<UInt32>::max() ||
        codecs.toDouble() != double(version) || !QFileInfo(target.physical_).isAbsolute() ||
        target.physical_.contains(QChar::Null) || layers.isEmpty() || layers.size() > 64)
        return {};
    target.version_ = UInt32(version);
    for (const auto &element : layers) {
        const auto layer = element.toObject();
        const auto index = layer.value("index");
        const qint64 raw = index.toInteger(-1);
        const QString path = layer.value("path").toString();
        const QString format = layer.value("format").toString();
        if (!index.isDouble() || raw < 0 || raw > std::numeric_limits<UInt32>::max() ||
            index.toDouble() != double(raw) || path.isEmpty() || path.contains(QChar::Null) ||
            format.isEmpty() || format.contains(QChar::Null))
            return {};
        target.layers_.push_back({{UInt32(raw), ToUString(path)}, format, false, {}});
    }
    target.settings_ = settingsPath;
    target.backendFactory_ = backend.independentBackendFactory();
    return target;
}

QtExternalArchiveTarget::UpdateResult QtExternalArchiveTarget::Update(const QString &file,
                                                                      QWidget *parent) const {
    if (!source_ || !source_->IsCurrentFor(physical_) || layers_.empty())
        return {E_FAIL, false, {}};
    std::unique_ptr<ArchiveBackend> backend(backendFactory_(nullptr));
    CMyComPtr<IFolderManager> manager = new QtArchiveFolderManager(*backend);
    CPanel panel;
    panel.AttachFolderManager(manager, settings_);
    panel.RebindWindowHost(parent, nullptr, nullptr);
    panel.AttachReloadPresentation([](const CSelectedState &) { return S_OK; });
    std::vector<CMyComPtr<IFolderFolder>> folders;
    bool removeTemporary = true;
    const auto close = qScopeGuard([&] {
        folders.clear();
        panel.ReleaseArchiveContext(removeTemporary);
    });
    const auto finish = [&](HRESULT result, std::optional<bool> published = false) {
        QStringList retained;
        if (!removeTemporary) {
            QString prefix = physical_;
            for (unsigned level = 1; level < panel.GetParentFolders().Size(); ++level) {
                prefix += QStringLiteral(" / ") + ToQString(layers_[level - 1].entry.path);
                const auto &link = panel.GetParentFolders()[level];
                const QString path = ToQString(fs2us(link.FilePath));
                retained.append(path);
                NWindows::NFile::NFind::CFileInfo info;
                if (link.OwnedTempLease && info.Find(link.FilePath)) {
                    const QtExternalFileRecord record{QFileInfo(path).fileName(),
                                                      ToQString(link.RelPath),
                                                      prefix,
                                                      {},
                                                      {info.Size, info.MTime},
                                                      {},
                                                      false};
                    link.OwnedTempLease->SetContext(record.Save());
                }
            }
        }
        return UpdateResult{result, published, retained};
    };
    COpenResult opened;
    panel.SetArchiveOpenPassword(
        layers_.front().usePassword ? std::optional(layers_.front().password) : std::nullopt);
    HRESULT result =
        panel.BindToPath(ToUString(physical_), ToUString(layers_.front().format), opened);
    if (result != S_OK || !panel.IsArcFolder() || panel.GetParentFolders().Size() != 1)
        return finish(result == S_OK ? E_FAIL : result);
    if (auto *resource = Resource(panel.GetFolder())) {
        const auto reader = resource->NativeReaderLease();
        if (!reader || reader->codecCatalog()->version() != version_ ||
            !reader->sourceIsCurrent() || !source_->IsCurrentFor(physical_))
            return finish(E_FAIL);
    } else
        return finish(E_FAIL);
    for (size_t level = 0; level < layers_.size(); ++level) {
        result = Prepare(panel, parent, settings_);
        if (result != S_OK)
            return finish(result);
        if (panel.IsThereReadOnlyFolder())
            return finish(E_FAIL);
        UInt32 index = 0;
        result = FindEntry(panel, layers_[level].entry.index, layers_[level].entry.path, index);
        if (result != S_OK)
            return finish(result);
        folders.emplace_back(panel.GetFolder());
        if (level + 1 == layers_.size())
            break;
        panel.SetArchiveOpenPassword(layers_[level + 1].usePassword
                                         ? std::optional(layers_[level + 1].password)
                                         : std::nullopt);
        const auto format = ToUString(layers_[level + 1].format);
        panel.LoadFullPath();
        result = panel.OpenItemInArchiveInternal(index, format.Ptr(), true);
        if (result != S_OK || panel.GetParentFolders().Size() != level + 2)
            return finish(result == S_OK ? E_FAIL : result);
    }
    // Reuse source inner-to-outer CopyFromFile transactions. Every reached
    // publication is checked; a failed inner step is never outer success.
    QString input = file;
    for (size_t remaining = layers_.size(); remaining != 0; --remaining) {
        const size_t level = remaining - 1;
        panel.SetNewFolder(folders[level]);
        result = Prepare(panel, parent, settings_);
        if (result != S_OK)
            return finish(result);
        if (panel.IsThereReadOnlyFolder())
            return finish(E_FAIL);
        UInt32 index = 0;
        result = FindEntry(panel, layers_[level].entry.index, layers_[level].entry.path, index);
        if (result != S_OK)
            return finish(result);
        auto *resource = Resource(panel.GetFolder());
        const auto reader = resource ? resource->NativeReaderLease() : nullptr;
        const auto expected = level == 0 ? source_
                              : reader   ? reader->SourceSnapshot()
                                         : std::nullopt;
        const auto &layer = layers_[level];
        const auto &openedLayer = panel.GetParentFolders()[unsigned(level)];
        const bool usePassword = layer.usePassword || openedLayer.UsePassword;
        const auto password = layer.usePassword ? layer.password : openedLayer.Password;
        const auto updated = panel.UpdateExternalFile(index, input.toStdWString().c_str(),
                                                      usePassword, password.native(), expected);
        if (updated.Published.value_or(false))
            removeTemporary = false;
        if (updated.Result != S_OK || updated.Published != std::optional<bool>(true))
            return finish(updated.Result == S_OK ? E_FAIL : updated.Result,
                          level == 0 ? updated.Published : std::optional<bool>(false));
        if (level != 0)
            input = ToQString(fs2us(panel.GetParentFolders()[unsigned(level)].FilePath));
    }
    removeTemporary = true;
    return finish(S_OK, true);
}
