#pragma once
#include "mount/smb/smbreadonlyserver.h"
#include "mount/smb/smbwakeup.h"
#include <QHash>
#include <QRegularExpression>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif
// The external headers require types, callbacks, then raw operations.
// clang-format off
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/smb2-errors.h>
// clang-format on

struct SmbReadOnlyServer::State : smb2_server {
    struct Node {
        SmbTreeEntry Entry;
        QString Path;
        QByteArray EncodedName, EncodedPath;
        QList<quint64> Children;
    };
    struct Handle {
        quint64 Item;
        qsizetype Cursor = 0;
        QString Pattern;
    };
    union Info {
        uint64_t Internal;
        uint32_t Scalar;
        smb2_file_position_info Position;
        smb2_file_name_info Name;
        smb2_file_basic_info Basic;
        smb2_file_standard_info Standard;
        smb2_file_all_info All;
        smb2_file_network_open_info Network;
        smb2_file_fs_volume_info Volume;
        smb2_file_fs_size_info Size;
        smb2_file_fs_full_size_info FullSize;
        smb2_file_fs_device_info Device;
        smb2_file_fs_attribute_info Attributes;
        smb2_file_attribute_tag_info Tag;
    };
    struct Client {
        State *Owner;
        quint64 Next = 1;
        std::map<quint64, Handle> Handles;
        Info Metadata;
        smb2_reparse_data_buffer Reparse{};
        QByteArray LinkName;
        std::vector<smb2_fileidbothdirectoryinformation> Directory;
        struct Pending {
            Pending(std::shared_ptr<std::atomic_uint> count, quint32 maximum,
                    std::shared_ptr<SmbWakeup> wakeup)
                : Maximum(maximum), Count(std::move(count)), Wakeup(std::move(wakeup)) {
                ++*Count;
            }
            ~Pending() { --*Count; }
            const quint32 Maximum;
            void complete(SmbTreeRead result) {
                {
                    std::lock_guard<std::mutex> lock(Mutex);
                    if (Result)
                        return;
                    Result = std::move(result);
                }
                Wakeup->notify();
            }
            std::optional<SmbTreeRead> takeResult() {
                std::lock_guard<std::mutex> lock(Mutex);
                if (!Result)
                    return std::nullopt;
                // Keep the slot engaged so late duplicate completions stay ignored.
                return std::move(*Result);
            }

          private:
            const std::shared_ptr<std::atomic_uint> Count;
            const std::shared_ptr<SmbWakeup> Wakeup;
            std::mutex Mutex;
            std::optional<SmbTreeRead> Result;
        };
        smb2_context *Context = nullptr;
        std::map<quint64, std::shared_ptr<Pending>> Reads;
        struct DirectoryPending {
            const quint64 Directory;
            const std::shared_ptr<SmbWakeup> Wakeup;
            const std::shared_ptr<std::atomic_uint> Count;
            const bool Create;
            smb2_create_request Open{};
            smb2_query_directory_request List{};
            QByteArray Name;
            std::mutex Mutex;
            std::optional<SmbTreeDirectory> Result;
            DirectoryPending(quint64 directory, std::shared_ptr<SmbWakeup> wakeup,
                             std::shared_ptr<std::atomic_uint> count, bool create)
                : Directory(directory), Wakeup(std::move(wakeup)), Count(std::move(count)),
                  Create(create) {
                ++*Count;
            }
            ~DirectoryPending() { --*Count; }
            void complete(SmbTreeDirectory result) {
                {
                    std::lock_guard<std::mutex> lock(Mutex);
                    if (Result)
                        return;
                    Result = std::move(result);
                }
                Wakeup->notify();
            }
        };
        std::map<quint64, std::shared_ptr<DirectoryPending>> MetadataRequests;
    };
    QList<SmbTreeEntry> Entries;
    QHash<quint64, Node> Nodes;
    QHash<QString, quint64> Paths;
    QByteArray User, Password;
    QString Share;
    Reader Read;
    AsyncReader ReadAsync;
    AsyncDirectoryReader DirectoryAsync;
    std::set<Client *> OpenClients;
    // Includes disconnected clients until their IPC completions expire.
    std::shared_ptr<std::atomic_uint> PendingCount = std::make_shared<std::atomic_uint>(0);
    std::shared_ptr<SmbWakeup> Wakeup = std::make_shared<SmbWakeup>();
    smb2_server_request_handlers Handlers{};
    std::atomic_bool Stop{false};
    bool Reported = false;
    bool Valid = false;
    bool CallbackFailed = false;
    unsigned Clients = 0;
    quint64 TotalBytes = 0;
    std::function<void(quint16)> Listening;
    std::function<void(QString)> Diagnostic;
    void trace(const QString &message) {
        if (Diagnostic)
            Diagnostic(message);
    }

    State(QList<SmbTreeEntry> entries, QString user, QString password, Reader reader,
          QString share);
    static quint64 handleId(const smb2_file_id fileId);
    static Client &client(smb2_context *context);
    static Handle *find(smb2_context *context, const smb2_file_id fileId);
    static int status(quint32 value) { return qint32(value); }
    static quint32 attributes(const SmbTreeEntry &entry);
    static bool validLink(const SmbTreeEntry &entry, const QString &path);
    static void timestamps(smb2_file_basic_info &info, const SmbTreeEntry &entry);
    int create(smb2_context *, smb2_create_request *, smb2_create_reply *,
               std::optional<quint64> message = {});
    int ioctl(smb2_context *, smb2_ioctl_request *, smb2_ioctl_reply *);
    int close(smb2_context *, smb2_close_request *, smb2_close_reply *);
    int read(smb2_context *, smb2_read_request *, smb2_read_reply *);
    int directory(smb2_context *, smb2_query_directory_request *, smb2_query_directory_reply *,
                  std::optional<quint64> message = {});
    int query(smb2_context *, smb2_query_info_request *, smb2_query_info_reply *);
    static int readReply(SmbTreeRead result, quint32 maximum, smb2_read_reply *reply);
    void completeReads();
    void completeMetadata();
    quint32 installDirectory(quint64 parent, const QList<SmbTreeEntry> &entries);
    int deferDirectory(smb2_context *, quint64 directory, smb2_create_request *,
                       smb2_query_directory_request *, std::optional<quint64> message);
};
