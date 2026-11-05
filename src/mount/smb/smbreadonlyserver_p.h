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
        void prepareWireMetadata();
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
    template <typename T> class Completion {
        const std::shared_ptr<SmbWakeup> wakeup_;
        std::mutex mutex_;
        std::optional<T> result_;

      public:
        explicit Completion(std::shared_ptr<SmbWakeup> wakeup) : wakeup_(std::move(wakeup)) {}
        void complete(T result) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (result_)
                    return;
                result_ = std::move(result);
            }
            wakeup_->notify();
        }
        std::optional<T> takeResult() {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!result_)
                return std::nullopt;
            // Keep the slot engaged so late duplicate completions stay ignored.
            return std::move(*result_);
        }
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
          private:
            const std::shared_ptr<std::atomic_uint> Count;

          public:
            const quint32 Maximum;
            Completion<SmbTreeRead> Result;
            Pending(std::shared_ptr<std::atomic_uint> count, quint32 maximum,
                    std::shared_ptr<SmbWakeup> wakeup)
                : Count(std::move(count)), Maximum(maximum), Result(std::move(wakeup)) {
                ++*Count;
            }
            ~Pending() { --*Count; }
        };
        smb2_context *Context = nullptr;
        std::map<quint64, std::shared_ptr<Pending>> Reads;
        struct DirectoryPending {
            const quint64 Directory;
            const std::shared_ptr<std::atomic_uint> Count;
            const bool Create;
            smb2_create_request Open{};
            smb2_query_directory_request List{};
            QByteArray Name;
            Completion<SmbTreeDirectory> Result;
            DirectoryPending(quint64 directory, std::shared_ptr<SmbWakeup> wakeup,
                             std::shared_ptr<std::atomic_uint> count, bool create)
                : Directory(directory), Count(std::move(count)), Create(create),
                  Result(std::move(wakeup)) {
                ++*Count;
            }
            ~DirectoryPending() { --*Count; }
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
    static bool validName(const QString &name);
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
