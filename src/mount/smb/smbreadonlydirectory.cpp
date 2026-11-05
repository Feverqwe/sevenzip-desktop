#include "mount/smb/smbreadonlyserver_p.h"
#include <QSet>
#include <algorithm>
#include <limits>

int SmbReadOnlyServer::State::deferDirectory(smb2_context *context, quint64 directory,
                                             smb2_create_request *open,
                                             smb2_query_directory_request *list,
                                             std::optional<quint64> message) {
    auto &session = client(context);
    if (PendingCount->load() >= 128 || session.MetadataRequests.size() + session.Reads.size() >= 32)
        return status(SMB2_STATUS_INSUFFICIENT_RESOURCES);
    const auto pending = std::make_shared<Client::DirectoryPending>(directory, Wakeup, PendingCount,
                                                                    open != nullptr);
    if (open) {
        pending->Open = *open;
        pending->Name = open->name ? open->name : "";
        pending->Open.name = pending->Name.constData();
        // Request contexts are not retained beyond the original decoded PDU.
        pending->Open.create_context = nullptr;
        pending->Open.create_context_length = 0;
    } else {
        pending->List = *list;
        pending->Name = list->name ? list->name : "";
        pending->List.name = pending->Name.constData();
    }
    const auto mid = message.value_or(smb2_get_last_request_message_id(context));
    if (!session.MetadataRequests.emplace(mid, pending).second)
        return status(SMB2_STATUS_INVALID_PARAMETER);
    try {
        DirectoryAsync(directory, [pending](SmbTreeDirectory result) {
            pending->Result.complete(std::move(result));
        });
    } catch (...) {
        session.MetadataRequests.erase(mid);
        throw;
    }
    return 1;
}

quint32 SmbReadOnlyServer::State::installDirectory(quint64 parent,
                                                   const QList<SmbTreeEntry> &entries) {
    const auto owner = Nodes.constFind(parent);
    if (owner == Nodes.cend() || !owner->Entry.Directory)
        return SMB2_STATUS_INVALID_PARAMETER;
    if (owner->Entry.ChildrenLoaded)
        return 0;
    QList<Node> additions;
    QSet<quint64> ids;
    QSet<QString> paths;
    for (const auto &entry : entries) {
        if (!entry.Id || entry.Parent != parent || entry.Id <= parent || Nodes.contains(entry.Id) ||
            ids.contains(entry.Id) || entry.Name.isEmpty() || entry.Name.size() > 255 ||
            entry.Name == "." || entry.Name == ".." || entry.Name.contains(u'/') ||
            entry.Name.contains(u'\\') || entry.Name.contains(u':') ||
            entry.Name.contains(QChar::Null))
            return SMB2_STATUS_IO_DEVICE_ERROR;
        Node node;
        node.Entry = entry;
        node.Entry.Modified -= node.Entry.Modified % 10;
        node.Path = owner->Path.isEmpty() ? entry.Name : owner->Path + u'/' + entry.Name;
        if (!validLink(entry, node.Path))
            return SMB2_STATUS_IO_DEVICE_ERROR;
        const auto key = node.Path.normalized(QString::NormalizationForm_C);
        if (node.Path.size() > 32767 || Paths.contains(key) || paths.contains(key))
            return SMB2_STATUS_IO_DEVICE_ERROR;
        node.EncodedName = entry.Name.toUtf8();
        QString windowsPath = u'\\' + node.Path;
        windowsPath.replace(u'/', u'\\');
        node.EncodedPath = windowsPath.toUtf8();
        paths.insert(key);
        ids.insert(entry.Id);
        additions.append(std::move(node));
    }
    for (auto &node : additions) {
        const auto entry = node.Entry;
        Paths.insert(node.Path.normalized(QString::NormalizationForm_C), entry.Id);
        Nodes.insert(entry.Id, std::move(node));
        Nodes[parent].Children.append(entry.Id);
        if (!entry.Directory)
            TotalBytes += std::min(entry.Size, std::numeric_limits<quint64>::max() - TotalBytes);
    }
    Nodes[parent].Entry.ChildrenLoaded = true;
    return 0;
}

void SmbReadOnlyServer::State::completeMetadata() {
    for (auto *session : OpenClients) {
        for (auto next = session->MetadataRequests.begin();
             next != session->MetadataRequests.end();) {
            const auto pending = next->second;
            auto result = pending->Result.takeResult();
            if (!result) {
                ++next;
                continue;
            }
            const auto message = next->first;
            next = session->MetadataRequests.erase(next);
            quint32 code = result->Status;
            if (!code)
                code = installDirectory(pending->Directory, result->Entries);
            smb2_pdu *reply = nullptr;
            const auto command = pending->Create ? SMB2_CREATE : SMB2_QUERY_DIRECTORY;
            if (!code && pending->Create) {
                smb2_create_reply opened{};
                const int status = create(session->Context, &pending->Open, &opened, message);
                if (status == 1)
                    continue;
                code = quint32(status);
                if (code == SMB2_STATUS_STOPPED_ON_SYMLINK && opened.symlink_target) {
                    reply = smb2_cmd_symlink_error_reply(session->Context, opened.symlink_target,
                                                         opened.symlink_unparsed, nullptr, nullptr);
                    code = 0;
                } else if (!code)
                    reply =
                        smb2_cmd_create_reply_async(session->Context, &opened, nullptr, nullptr);
            } else if (!code) {
                smb2_query_directory_reply listed{};
                code = quint32(directory(session->Context, &pending->List, &listed, message));
                if (!code && !listed.output_buffer_length)
                    code = SMB2_STATUS_NO_MORE_FILES;
                if (!code)
                    reply = smb2_cmd_query_directory_reply_async(session->Context, &pending->List,
                                                                 &listed, nullptr, nullptr);
            }
            if (code) {
                smb2_error_reply error{};
                reply = smb2_cmd_error_reply_async(session->Context, &error, command, code, nullptr,
                                                   nullptr);
            }
            if (!reply) {
                smb2_close_context(session->Context);
                break;
            }
            smb2_set_pdu_message_id(session->Context, reply, message);
            smb2_queue_pdu(session->Context, reply);
        }
    }
}
