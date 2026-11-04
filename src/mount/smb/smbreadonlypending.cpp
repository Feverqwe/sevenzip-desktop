#include "mount/smb/smbreadonlyserver_p.h"

// Only the library's server event loop owns contexts and queues replies. A
// completion may arrive from Qt's IPC thread after its client has disappeared;
// the shared result then expires without touching that freed context.
void SmbReadOnlyServer::State::completeReads() {
    for (auto *client : OpenClients) {
        for (auto next = client->Reads.begin(); next != client->Reads.end();) {
            const auto pending = next->second;
            auto result = pending->takeResult();
            if (!result) {
                ++next;
                continue;
            }
            smb2_read_reply reply{};
            if (Diagnostic)
                trace(QStringLiteral("READ complete message=%1 bytes=%2 status=%3")
                          .arg(next->first)
                          .arg(result->Data.size())
                          .arg(result->Status));
            const quint32 status = quint32(readReply(std::move(*result), pending->Maximum, &reply));
            smb2_pdu *pdu;
            if (status) {
                smb2_error_reply error{};
                pdu = smb2_cmd_error_reply_async(client->Context, &error, SMB2_READ, status,
                                                 nullptr, nullptr);
            } else {
                pdu = smb2_cmd_read_reply_async(client->Context, &reply, nullptr, nullptr);
            }
            if (pdu) {
                smb2_set_pdu_message_id(client->Context, pdu, next->first);
                smb2_queue_pdu(client->Context, pdu);
            } else {
                // No reply can be formed; drop this client so its OS receives
                // a transport failure rather than waiting forever.
                smb2_close_context(client->Context);
            }
            next = client->Reads.erase(next);
        }
    }
}
