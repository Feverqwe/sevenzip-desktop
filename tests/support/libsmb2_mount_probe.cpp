// Opt-in native client fixture using the same server adapter as the product.
// Fixed credentials belong to synthetic data, never to an OS account.
#include "mount/smb/smbreadonlyserver.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3)
        return 2;
    constexpr quint64 size = (quint64(5) << 30) + 123;
    const bool trace = argc == 3 && QString::fromUtf8(argv[2]) == "--trace";
    const bool caseNames = argc == 3 && QString::fromUtf8(argv[2]) == "--case-names";
    const bool async = argc == 3 && QString::fromUtf8(argv[2]) == "--async";
    const bool lazy = argc == 3 && QString::fromUtf8(argv[2]) == "--lazy";
    const bool links = argc == 3 && QString::fromUtf8(argv[2]) == "--links";
    QString failedTrace;
    if (argc == 3 && QString::fromUtf8(argv[2]) == "--throw-authorize")
        failedTrace = QStringLiteral("AUTHORIZE");
    else if (argc == 3 && QString::fromUtf8(argv[2]) == "--throw-tree")
        failedTrace = QStringLiteral("TREE_CONNECT");
    QList<SmbTreeEntry> entries{
        {1, 0, 0, 0, {}, true},
        {2, 1, 65536, 133800000000000007ULL, QStringLiteral("hello.txt"), false},
        {3, 1, 262144, 116444736000000000ULL, QStringLiteral("Файл с пробелом.bin"), false},
        {4, 1, size, 110000000000000000ULL, QStringLiteral("large.bin"), false}};
    if (caseNames) {
        entries.append({5, 1, 0, 0, QStringLiteral("xtables"), true});
        entries.append({6, 5, 4096, 0, QStringLiteral("libip6t_hl.so"), false});
        entries.append({7, 5, 4096, 0, QStringLiteral("libip6t_HL.so"), false});
    }
    if (lazy) {
        entries.append({5, 1, 0, 0, "lazy", true, false});
        entries.append({6, 1, 0, 0, "broken", true, false});
    }
    if (links) {
        entries.append({5, 1, 0, 0, "bin", true});
        entries.append({6, 5, 0, 0, "relative", false, true, "../hello.txt"});
        entries.append({7, 1, 0, 0, "dangling", false, true, "missing"});
        entries.append({8, 1, 0, 0, "cycle", false, true, "cycle"});
        entries.append(
            {9, 1, 0, 0, "unicode", false, true, QString::fromUtf8("Файл с пробелом.bin")});
    }
    const auto read = [trace, caseNames, async](quint64 id, quint64 offset, quint32 length) {
        if (trace)
            std::fprintf(stderr, "fixture read\n");
        if (async && id == 4)
            return SmbTreeRead{0xc0000185U, {}};
        SmbTreeRead result;
        result.Data.resize(length);
        for (quint32 i = 0; i < length; ++i)
            result.Data[i] = char((offset + i + (caseNames ? id : 0)) % 251);
        return result;
    };
    std::vector<std::jthread> pendingReads;
    SmbReadOnlyServer server(std::move(entries), QStringLiteral("szprobe"),
                             QStringLiteral("LocalProbeOnly427"), read);
    if (async)
        server.setAsyncReader([&](quint64 id, quint64 offset, quint32 length,
                                  SmbReadOnlyServer::ReadCompletion complete) {
            pendingReads.emplace_back([=] {
                complete(read(id, offset, length));
                // A late completion must not replace the first bytes or error.
                complete({0, QByteArray(length, 'X')});
            });
        });
    if (lazy)
        server.setAsyncDirectoryReader(
            [&, failed = 0](quint64 id, SmbReadOnlyServer::DirectoryCompletion complete) mutable {
                std::fprintf(stderr, "lazy directory %llu\n", static_cast<unsigned long long>(id));
                SmbTreeDirectory result;
                if (id == 5)
                    result.Entries = {{7, 5, 0, 0, "nested", true, false},
                                      {8, 5, 128, 0, "direct.bin", false}};
                else if (id == 7)
                    result.Entries = {{9, 7, 128, 0, QString::fromUtf8("данные 空.bin"), false}};
                else if (id == 6 && ++failed == 1)
                    result.Status = 0xc0000185U;
                else if (id == 6 && failed == 2)
                    result.Entries = {{10, 6, 10, 0, "../escape", false}};
                pendingReads.emplace_back([id, result, complete] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(id == 5 ? 1000 : 25));
                    complete(result);
                    complete({0xc0000185U, {}});
                });
            });
    QString error;
    if (trace)
        server.setDiagnosticHandler(
            [](const QString &message) { std::fprintf(stderr, "%s\n", qPrintable(message)); });
    if (!failedTrace.isEmpty())
        server.setDiagnosticHandler([failedTrace, failed = false](const QString &message) mutable {
            if (!failed && message.startsWith(failedTrace)) {
                failed = true;
                throw std::runtime_error("fixture callback failure");
            }
        });
    if (!server.validate(error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 2;
    }
    return server.serve(quint16(std::strtoul(argv[1], nullptr, 10)),
                        [](quint16 port) {
                            std::fprintf(stderr, "loopback SMB fixture ready on port %u\n", port);
                        }) == 0
               ? 0
               : 1;
}
