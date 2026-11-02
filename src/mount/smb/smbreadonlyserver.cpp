#include "mount/smb/smbmountconstants.h"
#include "mount/smb/smbreadonlyserver_p.h"
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

#ifdef SEVENZIP_USE_SMB2_MODULE
import sevenzip.dependencies.smb2;
#else
#include "dependencies/smb2/facade.h"
#endif

namespace {
QString Key(const QString &path) { return path.normalized(QString::NormalizationForm_C); }
template <typename Work> int Guard(Work work) noexcept {
    try {
        return work();
    } catch (...) {
        return qint32(SMB2_STATUS_INSUFFICIENT_RESOURCES);
    }
}
} // namespace

SmbReadOnlyServer::State::State(QList<SmbTreeEntry> entries, QString user, QString password,
                                Reader reader, QString share)
    : smb2_server{}, Entries(std::move(entries)), User(user.toUtf8()), Password(password.toUtf8()),
      Share(std::move(share)), Read(std::move(reader)) {
    handlers = &Handlers;
    signing_enabled = 1;
    // A virtual server has its own identity, independent of the OS account host.
    std::strncpy(hostname, kSevenzipSmbServerName, sizeof(hostname) - 1);
    std::strncpy(domain, "Sevenzip", sizeof(domain) - 1);
    const auto uuid = QUuid::createUuid().toRfc4122();
    std::memcpy(guid, uuid.constData(), sizeof(guid));
    Handlers.authorize_user = [](smb2_server *server, smb2_context *context, const char *name,
                                 const char *, const char *) {
        return Guard([&] {
            auto &owner = *static_cast<State *>(server);
            owner.trace(QStringLiteral("AUTHORIZE accepted=%1").arg(name && owner.User == name));
            if (!name || owner.User != name)
                return -1;
            smb2_set_password(context, owner.Password.constData());
            return 0;
        });
    };
    Handlers.session_established = [](smb2_server *server, smb2_context *) {
        return Guard([&] {
            static_cast<State *>(server)->trace(QStringLiteral("Authenticated session"));
            return 0;
        });
    };
    Handlers.destruction_event = [](smb2_server *, smb2_context *context) {
        auto *closed = static_cast<Client *>(smb2_get_opaque(context));
        if (closed) {
            --closed->Owner->Clients;
            closed->Owner->OpenClients.erase(closed);
            delete closed;
        }
        smb2_set_opaque(context, nullptr);
        return 0;
    };
    Handlers.logoff_cmd = [](smb2_server *, smb2_context *context) {
        client(context).Handles.clear();
        return 0;
    };
    Handlers.tree_connect_cmd = [](smb2_server *server, smb2_context *,
                                   smb2_tree_connect_request *request,
                                   smb2_tree_connect_reply *reply) {
        return Guard([&] {
            static_cast<State *>(server)->trace(QStringLiteral("TREE_CONNECT"));
            if (!request->path || (request->path_length & 1))
                return status(SMB2_STATUS_INVALID_PARAMETER);
            const auto decoded = sevenzip::smb2::decode_name(
                {request->path, static_cast<std::size_t>(request->path_length / 2)});
            if (!decoded)
                return status(SMB2_STATUS_INVALID_PARAMETER);
            QString path = QString::fromUtf8(decoded->get());
            path.replace(u'\\', u'/');
            if (path.section(u'/', -1).compare(static_cast<State *>(server)->Share,
                                               Qt::CaseInsensitive))
                return status(SMB2_STATUS_BAD_NETWORK_NAME);
            reply->share_type = SMB2_SHARE_TYPE_DISK;
            reply->maximal_access = SMB2_FILE_READ_DATA | SMB2_FILE_READ_EA |
                                    SMB2_FILE_READ_ATTRIBUTES | SMB2_FILE_EXECUTE |
                                    SMB2_READ_CONTROL | SMB2_SYNCHRONIZE;
            return 0;
        });
    };
    Handlers.tree_disconnect_cmd = [](smb2_server *, smb2_context *context, uint32_t) {
        client(context).Handles.clear();
        return 0;
    };
    Handlers.create_cmd = [](smb2_server *server, smb2_context *context,
                             smb2_create_request *request, smb2_create_reply *reply) {
        return Guard([&] { return static_cast<State *>(server)->create(context, request, reply); });
    };
    Handlers.close_cmd = [](smb2_server *server, smb2_context *context, smb2_close_request *request,
                            smb2_close_reply *reply) {
        return Guard([&] { return static_cast<State *>(server)->close(context, request, reply); });
    };
    Handlers.read_cmd = [](smb2_server *server, smb2_context *context, smb2_read_request *request,
                           smb2_read_reply *reply) {
        return Guard([&] { return static_cast<State *>(server)->read(context, request, reply); });
    };
    Handlers.query_directory_cmd = [](smb2_server *server, smb2_context *context,
                                      smb2_query_directory_request *request,
                                      smb2_query_directory_reply *reply) {
        return Guard(
            [&] { return static_cast<State *>(server)->directory(context, request, reply); });
    };
    Handlers.query_info_cmd = [](smb2_server *server, smb2_context *context,
                                 smb2_query_info_request *request, smb2_query_info_reply *reply) {
        return Guard([&] { return static_cast<State *>(server)->query(context, request, reply); });
    };
    Handlers.flush_cmd = [](smb2_server *, smb2_context *context, smb2_flush_request *request) {
        return find(context, request->file_id) ? 0 : status(SMB2_STATUS_FILE_CLOSED);
    };
    Handlers.write_cmd = [](smb2_server *, smb2_context *, smb2_write_request *,
                            smb2_write_reply *) {
        return status(SMB2_STATUS_MEDIA_WRITE_PROTECTED);
    };
    Handlers.set_info_cmd = [](smb2_server *, smb2_context *, smb2_set_info_request *) {
        return status(SMB2_STATUS_MEDIA_WRITE_PROTECTED);
    };
    Handlers.ioctl_cmd = [](smb2_server *, smb2_context *, smb2_ioctl_request *,
                            smb2_ioctl_reply *) { return status(SMB2_STATUS_NOT_SUPPORTED); };
    Handlers.echo_cmd = [](smb2_server *, smb2_context *) { return 0; };
    extra_service = [](smb2_server *server, fd_set *, fd_set *) {
        auto &state = *static_cast<State *>(server);
        const int result = Guard([&] {
            state.completeReads();
            if (state.Stop)
                state.stop_requested = 1;
            if (state.Reported)
                return 0;
            sockaddr_in address{};
#ifdef _WIN32
            int size = sizeof(address);
#else
            socklen_t size = sizeof(address);
#endif
            if (::getsockname(state.fd, reinterpret_cast<sockaddr *>(&address), &size) == 0) {
                state.Reported = true;
                if (state.Listening)
                    state.Listening(ntohs(address.sin_port));
            }
            return 0;
        });
        if (result) {
            state.CallbackFailed = true;
            state.stop_requested = 1;
        }
    };
}

quint64 SmbReadOnlyServer::State::handleId(const smb2_file_id id) {
    quint64 value;
    std::memcpy(&value, id, sizeof(value));
    return value;
}
SmbReadOnlyServer::State::Client &SmbReadOnlyServer::State::client(smb2_context *context) {
    return *static_cast<Client *>(smb2_get_opaque(context));
}
SmbReadOnlyServer::State::Handle *SmbReadOnlyServer::State::find(smb2_context *context,
                                                                 const smb2_file_id id) {
    auto &handles = client(context).Handles;
    const auto found = handles.find(handleId(id));
    return found == handles.end() ? nullptr : &found->second;
}
quint32 SmbReadOnlyServer::State::attributes(const SmbTreeEntry &entry) {
    return SMB2_FILE_ATTRIBUTE_READONLY | (entry.Directory ? SMB2_FILE_ATTRIBUTE_DIRECTORY : 0);
}

SmbReadOnlyServer::SmbReadOnlyServer(QList<SmbTreeEntry> entries, QString user, QString password,
                                     Reader reader, QString share)
    : state_(std::make_unique<State>(std::move(entries), std::move(user), std::move(password),
                                     std::move(reader), std::move(share))) {}
SmbReadOnlyServer::~SmbReadOnlyServer() = default;

bool SmbReadOnlyServer::validate(QString &error) {
    auto &state = *state_;
    state.Valid = false;
    if (state.Entries.isEmpty() || state.Entries.size() > 1000000 || state.User.isEmpty() ||
        state.Password.isEmpty() || state.User.size() > 128 || state.Password.size() > 1024 ||
        state.User.contains('\0') || state.Password.contains('\0') || !state.Read) {
        error = QStringLiteral("Invalid read-only server session.");
        return false;
    }
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"))
             .match(state.Share)
             .hasMatch()) {
        error = QStringLiteral("Invalid local share name.");
        return false;
    }
    state.Nodes.clear();
    state.Paths.clear();
    state.TotalBytes = 0;
    for (qsizetype i = 0; i < state.Entries.size(); ++i) {
        const auto &entry = state.Entries[i];
        const bool root = i == 0;
        if (entry.Id != quint64(i) + 1 ||
            (root && (entry.Parent || !entry.Name.isEmpty() || !entry.Directory)) ||
            (!root && (!entry.Parent || entry.Parent >= entry.Id ||
                       !state.Nodes[entry.Parent].Entry.Directory || entry.Name.isEmpty() ||
                       entry.Name.size() > 255 || entry.Name == "." || entry.Name == ".." ||
                       entry.Name.contains(u'/') || entry.Name.contains(u'\\') ||
                       entry.Name.contains(u':') || entry.Name.contains(QChar::Null)))) {
            error = QStringLiteral("Invalid virtual directory entry.");
            return false;
        }
        State::Node node;
        node.Entry = entry;
        // The library's public timestamp structures carry microseconds. Use
        // the same precision in CREATE's raw FILETIME fields and QUERY replies.
        node.Entry.Modified -= node.Entry.Modified % 10;
        node.Path = root ? QString() : state.Nodes[entry.Parent].Path;
        if (!node.Path.isEmpty())
            node.Path += u'/';
        node.Path += entry.Name;
        node.EncodedName = entry.Name.toUtf8();
        QString windowsPath = u'\\' + node.Path;
        windowsPath.replace(u'/', u'\\');
        node.EncodedPath = windowsPath.toUtf8();
        const auto key = Key(node.Path);
        if (state.Paths.contains(key)) {
            error = QStringLiteral("Conflicting virtual directory entries.");
            return false;
        }
        state.Nodes.insert(entry.Id, std::move(node));
        state.Paths.insert(key, entry.Id);
        if (!root)
            state.Nodes[entry.Parent].Children.append(entry.Id);
        if (!entry.Directory)
            state.TotalBytes +=
                std::min(entry.Size, std::numeric_limits<quint64>::max() - state.TotalBytes);
    }
    state.Valid = true;
    return true;
}

int SmbReadOnlyServer::serve(quint16 port, std::function<void(quint16)> listening) {
#ifdef _WIN32
    WSADATA sockets;
    const int started = WSAStartup(MAKEWORD(2, 2), &sockets);
    if (started)
        return started;
    const auto cleanupSockets = qScopeGuard([] { WSACleanup(); });
#endif
    if (state_->Stop)
        return 0;
    if (!state_->Valid)
        return -1;
    state_->port = port;
    state_->Listening = std::move(listening);
    auto connection = [state = state_.get()](smb2_context &session) noexcept {
        auto *context = &session;
        if (state->Clients >= 32) {
            smb2_close_context(context);
            return;
        }
        const int result = Guard([&] {
            auto client = std::make_unique<State::Client>();
            client->Owner = state;
            client->Context = context;
            state->OpenClients.insert(client.get());
            ++state->Clients;
            smb2_set_opaque(context, client.release());
            return 0;
        });
        if (result) {
            smb2_close_context(context);
            return;
        }
        if (state->Diagnostic)
            smb2_register_error_callback(context, [](smb2_context *ctx, const char *error) {
                if (const auto *session = static_cast<State::Client *>(smb2_get_opaque(ctx))) {
                    const int result = Guard([&] {
                        session->Owner->trace(QString::fromUtf8(error));
                        return 0;
                    });
                    if (result) {
                        session->Owner->CallbackFailed = true;
                        session->Owner->stop_requested = 1;
                    }
                }
            });
        // Only this server dialect is accepted so far. Signing
        // remains mandatory; no client policy is changed.
        smb2_set_version(context, SMB2_VERSION_0210);
        smb2_set_sign(context, 1);
    };
    const auto result = sevenzip::smb2::serve(*state_, 16, connection);
    return state_->CallbackFailed ? -1 : result ? 0 : result.error();
}
void SmbReadOnlyServer::requestStop() { state_->Stop = true; }
void SmbReadOnlyServer::setDiagnosticHandler(std::function<void(QString)> handler) {
    state_->Diagnostic = std::move(handler);
}
void SmbReadOnlyServer::setAsyncReader(AsyncReader reader) {
    state_->ReadAsync = std::move(reader);
}

int SmbReadOnlyServer::State::create(smb2_context *context, smb2_create_request *request,
                                     smb2_create_reply *reply) {
    trace(QStringLiteral("CREATE access=%1 options=%2 disposition=%3")
              .arg(request->desired_access, 0, 16)
              .arg(request->create_options, 0, 16)
              .arg(request->create_disposition));
    constexpr quint32 writes = SMB2_FILE_WRITE_DATA | SMB2_FILE_APPEND_DATA | SMB2_FILE_WRITE_EA |
                               SMB2_FILE_WRITE_ATTRIBUTES | SMB2_FILE_DELETE_CHILD | SMB2_DELETE |
                               SMB2_WRITE_DACL | SMB2_WRITE_OWNER | SMB2_GENERIC_WRITE |
                               SMB2_GENERIC_ALL;
    if ((request->desired_access & writes) ||
        (request->create_options & SMB2_FILE_DELETE_ON_CLOSE) ||
        (request->create_disposition != SMB2_FILE_OPEN &&
         request->create_disposition != SMB2_FILE_OPEN_IF))
        return status(SMB2_STATUS_MEDIA_WRITE_PROTECTED);
    QString path = QString::fromUtf8(request->name ? request->name : "");
    path.replace(u'\\', u'/');
    if (path == "/")
        path.clear();
    const auto found = Paths.constFind(Key(path));
    if (found == Paths.cend())
        return status(SMB2_STATUS_OBJECT_NAME_NOT_FOUND);
    const auto &entry = Nodes[*found].Entry;
    if ((request->create_options & SMB2_FILE_DIRECTORY_FILE) && !entry.Directory)
        return status(SMB2_STATUS_NOT_A_DIRECTORY);
    if ((request->create_options & SMB2_FILE_NON_DIRECTORY_FILE) && entry.Directory)
        return status(SMB2_STATUS_FILE_IS_A_DIRECTORY);
    auto &session = client(context);
    if (session.Handles.size() >= 1024)
        return status(SMB2_STATUS_INSUFFICIENT_RESOURCES);
    const quint64 handle = session.Next++;
    session.Handles.emplace(handle, Handle{entry.Id});
    std::memcpy(reply->file_id, &handle, sizeof(handle));
    reply->create_action = 1; // FILE_OPENED
    reply->file_attributes = attributes(entry);
    reply->allocation_size = reply->end_of_file = entry.Size;
    reply->creation_time = reply->last_access_time = reply->last_write_time = reply->change_time =
        entry.Modified;
    return 0;
}

int SmbReadOnlyServer::State::close(smb2_context *context, smb2_close_request *request,
                                    smb2_close_reply *reply) {
    const auto *opened = find(context, request->file_id);
    if (!opened)
        return status(SMB2_STATUS_FILE_CLOSED);
    const auto &entry = Nodes[opened->Item].Entry;
    reply->file_attributes = attributes(entry);
    reply->allocation_size = reply->end_of_file = entry.Size;
    client(context).Handles.erase(handleId(request->file_id));
    return 0;
}

int SmbReadOnlyServer::State::read(smb2_context *context, smb2_read_request *request,
                                   smb2_read_reply *reply) {
    const auto *opened = find(context, request->file_id);
    if (!opened)
        return status(SMB2_STATUS_FILE_CLOSED);
    const auto &entry = Nodes[opened->Item].Entry;
    if (entry.Directory)
        return status(SMB2_STATUS_FILE_IS_A_DIRECTORY);
    if (request->length > 1024 * 1024)
        return status(SMB2_STATUS_INVALID_PARAMETER);
    if (!request->length)
        return 0;
    if (request->offset >= entry.Size)
        return status(SMB2_STATUS_END_OF_FILE);
    const auto length = quint32(std::min<quint64>(request->length, entry.Size - request->offset));
    if (ReadAsync) {
        auto &session = client(context);
        if (PendingCount->load() >= 128 || session.Reads.size() >= 32)
            return status(SMB2_STATUS_INSUFFICIENT_RESOURCES);
        const auto pending = std::make_shared<Client::Pending>(PendingCount, length);
        const quint64 message = smb2_get_last_request_message_id(context);
        if (!session.Reads.emplace(message, pending).second)
            return status(SMB2_STATUS_INVALID_PARAMETER);
        try {
            ReadAsync(entry.Id, request->offset, length,
                      [pending](SmbTreeRead result) { pending->complete(std::move(result)); });
        } catch (...) {
            session.Reads.erase(message);
            throw;
        }
        return 1;
    }
    return readReply(Read(entry.Id, request->offset, length), length, reply);
}

int SmbReadOnlyServer::State::readReply(SmbTreeRead result, quint32 maximum,
                                        smb2_read_reply *reply) {
    if (result.Status)
        return status(result.Status);
    if (result.Data.isEmpty() || quint64(result.Data.size()) > maximum)
        return status(SMB2_STATUS_IO_DEVICE_ERROR);
    auto *data = static_cast<uint8_t *>(std::malloc(size_t(result.Data.size())));
    if (!data)
        return status(SMB2_STATUS_INSUFFICIENT_RESOURCES);
    std::memcpy(data, result.Data.constData(), size_t(result.Data.size()));
    reply->data = data;
    reply->data_length = uint32_t(result.Data.size());
    return 0;
}
