#include "mount/smb/smbreadonlyserver_p.h"
#include <algorithm>
#include <cstring>

void SmbReadOnlyServer::State::timestamps(smb2_file_basic_info &info, const SmbTreeEntry &entry) {
    info.file_attributes = attributes(entry);
    smb2_win_to_timeval(entry.Modified, &info.creation_time);
    info.last_access_time = info.last_write_time = info.change_time = info.creation_time;
}

int SmbReadOnlyServer::State::directory(smb2_context *context,
                                        smb2_query_directory_request *request,
                                        smb2_query_directory_reply *reply,
                                        std::optional<quint64> message) {
    auto *opened = find(context, request->file_id);
    if (!opened)
        return status(SMB2_STATUS_FILE_CLOSED);
    const auto &node = Nodes[opened->Item];
    if (!node.Entry.Directory)
        return status(SMB2_STATUS_NOT_A_DIRECTORY);
    if (!node.Entry.ChildrenLoaded && DirectoryAsync)
        return deferDirectory(context, opened->Item, nullptr, request, message);
    const auto infoClass = request->file_information_class;
    trace(QStringLiteral("DIRECTORY class=%1").arg(infoClass));
    if (infoClass != SMB2_FILE_DIRECTORY_INFORMATION &&
        infoClass != SMB2_FILE_FULL_DIRECTORY_INFORMATION &&
        infoClass != SMB2_FILE_BOTH_DIRECTORY_INFORMATION &&
        infoClass != SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION &&
        infoClass != SMB2_FILE_ID_BOTH_DIRECTORY_INFORMATION)
        return status(SMB2_STATUS_INVALID_INFO_CLASS);
    if (request->flags & (SMB2_RESTART_SCANS | SMB2_REOPEN)) {
        opened->Cursor = 0;
        opened->Pattern = QString::fromUtf8(request->name ? request->name : "");
    }
    if (opened->Pattern.isEmpty())
        opened->Pattern = QString::fromUtf8(request->name ? request->name : "*");
    if (request->flags & SMB2_INDEX_SPECIFIED)
        opened->Cursor = request->file_index;
    QString pattern = opened->Pattern;
    if (pattern.isEmpty() || pattern == "*.*")
        pattern = QStringLiteral("*");
    const QRegularExpression match(QRegularExpression::wildcardToRegularExpression(pattern));
    if (!match.isValid())
        return status(SMB2_STATUS_INVALID_PARAMETER);
    auto &output = client(context).Directory;
    output.clear();
    quint32 bytes = 0;
    while (opened->Cursor < node.Children.size() && output.size() < 128) {
        const auto &child = Nodes[node.Children.at(opened->Cursor)];
        if (!match.match(child.Entry.Name).hasMatch()) {
            ++opened->Cursor;
            continue;
        }
        const quint32 fixed = infoClass == SMB2_FILE_DIRECTORY_INFORMATION
                                  ? SMB2_DIRECTORY_INFORMATION_SIZE
                              : infoClass == SMB2_FILE_FULL_DIRECTORY_INFORMATION
                                  ? SMB2_FULL_DIRECTORY_INFORMATION_SIZE
                              : infoClass == SMB2_FILE_BOTH_DIRECTORY_INFORMATION
                                  ? SMB2_BOTH_DIRECTORY_INFORMATION_SIZE
                              : infoClass == SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION
                                  ? SMB2_FILEID_FULL_DIRECTORY_INFORMATION_SIZE
                                  : SMB2_FILEID_BOTH_DIRECTORY_INFORMATION_SIZE;
        const quint32 required = (fixed + quint32(child.Entry.Name.size() * 2) + 7) & ~quint32(7);
        if (required > request->output_buffer_length - bytes) {
            if (output.empty())
                return status(SMB2_STATUS_BUFFER_TOO_SMALL);
            break;
        }
        smb2_fileidbothdirectoryinformation entry{};
        entry.file_index = quint32(opened->Cursor);
        entry.file_id = child.Entry.Id;
        const auto &exported = child.Entry;
        entry.file_attributes = attributes(exported);
        if (!exported.SymlinkTarget.isEmpty())
            entry.ea_size = SMB2_REPARSE_TAG_SYMLINK;
        entry.allocation_size = entry.end_of_file = exported.Size;
        entry.name = child.EncodedName.constData();
        smb2_win_to_timeval(child.Entry.Modified, &entry.creation_time);
        entry.last_access_time = entry.last_write_time = entry.change_time = entry.creation_time;
        output.push_back(entry);
        bytes += required;
        ++opened->Cursor;
        if (request->flags & SMB2_RETURN_SINGLE_ENTRY)
            break;
    }
    if (!output.empty()) {
        reply->output_buffer = reinterpret_cast<uint8_t *>(output.data());
        reply->output_buffer_length = uint32_t(output.size() * sizeof(output.front()));
    }
    return 0;
}

int SmbReadOnlyServer::State::query(smb2_context *context, smb2_query_info_request *request,
                                    smb2_query_info_reply *reply) {
    const auto *opened = find(context, request->file_id);
    if (!opened)
        return status(SMB2_STATUS_FILE_CLOSED);
    const auto &node = Nodes[opened->Item];
    const auto &entry = node.Entry;
    trace(QStringLiteral("QUERY type=%1 class=%2 item=%3")
              .arg(request->info_type)
              .arg(request->file_info_class)
              .arg(entry.Id));
    auto &info = client(context).Metadata;
    std::memset(&info, 0, sizeof(info));
    reply->output_buffer = &info;
    if (request->info_type == SMB2_0_INFO_FILESYSTEM) {
        switch (request->file_info_class) {
        case SMB2_FILE_FS_VOLUME_INFORMATION:
            info.Volume.volume_label = reinterpret_cast<const uint8_t *>("Sevenzip");
            info.Volume.volume_label_length = 8;
            info.Volume.volume_serial_number = 1;
            reply->output_buffer_length = sizeof(info.Volume);
            return 0;
        case SMB2_FILE_FS_ATTRIBUTE_INFORMATION:
            info.Attributes.filesystem_name = reinterpret_cast<const uint8_t *>("Sevenzip");
            info.Attributes.filesystem_name_length = 8;
            info.Attributes.maximum_component_name_length = 255;
            info.Attributes.filesystem_attributes = 0x00080087;
            // macOS uses its Minshall–French compatibility path when the
            // server does not advertise native reparse points.
            reply->output_buffer_length = sizeof(info.Attributes);
            return 0;
        case SMB2_FILE_FS_SIZE_INFORMATION:
            info.Size.total_allocation_units = TotalBytes / 512 + (TotalBytes % 512 != 0);
            info.Size.sectors_per_allocation_unit = 1;
            info.Size.bytes_per_sector = 512;
            reply->output_buffer_length = sizeof(info.Size);
            return 0;
        case SMB2_FILE_FS_FULL_SIZE_INFORMATION:
            info.FullSize.total_allocation_units = TotalBytes / 512 + (TotalBytes % 512 != 0);
            info.FullSize.sectors_per_allocation_unit = 1;
            info.FullSize.bytes_per_sector = 512;
            reply->output_buffer_length = sizeof(info.FullSize);
            return 0;
        case SMB2_FILE_FS_DEVICE_INFORMATION:
            info.Device.device_type = FILE_DEVICE_DISK;
            info.Device.characteristics = FILE_READ_ONLY_DEVICE;
            reply->output_buffer_length = sizeof(info.Device);
            return 0;
        }
    } else if (request->info_type == SMB2_0_INFO_FILE) {
        switch (request->file_info_class) {
        case SMB2_FILE_NAME_INFORMATION:
        case SMB2_FILE_NORMALIZED_NAME_INFORMATION:
            info.Name.name = reinterpret_cast<const uint8_t *>(node.EncodedPath.constData());
            info.Name.file_name_length = uint32_t((node.Path.size() + 1) * 2);
            reply->output_buffer_length = uint32_t(sizeof(info.Name) + info.Name.file_name_length);
            return 0;
        case SMB2_FILE_EA_INFORMATION:
        case SMB2_FILE_MODE_INFORMATION:
        case SMB2_FILE_ALIGNMENT_INFORMATION:
            reply->output_buffer_length = sizeof(info.Scalar);
            return 0;
        case SMB2_FILE_ACCESS_INFORMATION:
            info.Scalar = SMB2_FILE_READ_DATA | SMB2_FILE_READ_EA | SMB2_FILE_READ_ATTRIBUTES |
                          SMB2_FILE_EXECUTE | SMB2_READ_CONTROL | SMB2_SYNCHRONIZE;
            reply->output_buffer_length = sizeof(info.Scalar);
            return 0;
        case SMB2_FILE_POSITION_INFORMATION:
            reply->output_buffer_length = sizeof(info.Position);
            return 0;
        case SMB2_FILE_INTERNAL_INFORMATION:
            info.Internal = entry.Id;
            reply->output_buffer_length = sizeof(info.Internal);
            return 0;
        case SMB2_FILE_ALL_INFORMATION:
            timestamps(info.All.basic, entry);
            info.All.standard.directory = entry.Directory;
            info.All.standard.end_of_file = info.All.standard.allocation_size = entry.Size;
            info.All.standard.number_of_links = 1;
            info.All.index_number = entry.Id;
            info.All.access_flags =
                SMB2_FILE_READ_DATA | SMB2_FILE_READ_ATTRIBUTES | SMB2_READ_CONTROL;
            info.All.name = reinterpret_cast<const uint8_t *>(node.EncodedPath.constData());
            reply->output_buffer_length = uint32_t(sizeof(info.All) + (node.Path.size() + 1) * 2);
            return 0;
        case SMB2_FILE_BASIC_INFORMATION:
            timestamps(info.Basic, entry);
            reply->output_buffer_length = sizeof(info.Basic);
            return 0;
        case SMB2_FILE_STANDARD_INFORMATION:
            info.Standard.directory = entry.Directory;
            info.Standard.end_of_file = info.Standard.allocation_size = entry.Size;
            info.Standard.number_of_links = 1;
            reply->output_buffer_length = sizeof(info.Standard);
            return 0;
        case SMB2_FILE_NETWORK_OPEN_INFORMATION:
            info.Network.file_attributes = attributes(entry);
            info.Network.end_of_file = info.Network.allocation_size = entry.Size;
            smb2_win_to_timeval(entry.Modified, &info.Network.creation_time);
            info.Network.last_access_time = info.Network.last_write_time =
                info.Network.change_time = info.Network.creation_time;
            reply->output_buffer_length = sizeof(info.Network);
            return 0;
        case SMB2_FILE_ATTRIBUTE_TAG_INFORMATION:
            info.Tag.file_attributes = attributes(entry);
            info.Tag.reparse_tag = entry.SymlinkTarget.isEmpty() ? 0 : SMB2_REPARSE_TAG_SYMLINK;
            reply->output_buffer_length = sizeof(info.Tag);
            return 0;
        }
    }
    return status(SMB2_STATUS_NOT_SUPPORTED);
}

int SmbReadOnlyServer::State::ioctl(smb2_context *context, smb2_ioctl_request *request,
                                    smb2_ioctl_reply *reply) {
    trace(QStringLiteral("IOCTL code=%1 maximum=%2")
              .arg(request->ctl_code, 0, 16)
              .arg(request->max_output_response));
    if (request->ctl_code != SMB2_FSCTL_GET_REPARSE_POINT)
        return status(SMB2_STATUS_NOT_SUPPORTED);
    const auto *opened = find(context, request->file_id);
    if (!opened)
        return status(SMB2_STATUS_FILE_CLOSED);
    const auto &target = Nodes[opened->Item].Entry.SymlinkTarget;
    if (target.isEmpty())
        return status(SMB2_STATUS_NOT_A_REPARSE_POINT);
    QString windowsTarget = target;
    windowsTarget.replace(u'/', u'\\');
    const quint32 bytes = 24 + quint32(windowsTarget.size()) * 2;
    if (bytes > request->max_output_response)
        return status(SMB2_STATUS_BUFFER_TOO_SMALL);
    auto &session = client(context);
    session.LinkName = windowsTarget.toUtf8();
    session.Reparse = {};
    session.Reparse.reparse_tag = SMB2_REPARSE_TAG_SYMLINK;
    session.Reparse.symlink.flags = SMB2_SYMLINK_FLAG_RELATIVE;
    session.Reparse.symlink.subname = session.LinkName.data();
    session.Reparse.symlink.printname = nullptr;
    reply->output = &session.Reparse;
    reply->output_count = bytes;
    reply->flags = request->flags;
    return 0;
}
