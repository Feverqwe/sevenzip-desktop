// Exercise the canonical C encoder with allocation failures, without a server.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>

// These external headers are order dependent.
// clang-format off
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include "compat.h"
#include "libsmb2/include/libsmb2-private.h"
// clang-format on

enum Failure { None, Pdu, Header, HeaderVector, PayloadVector, Padding };
static enum Failure failure;
static void *payload;
static unsigned releases;

static void *probe_calloc(size_t count, size_t size) {
    return failure == Header ? NULL : calloc(count, size);
}
static void probe_free(void *memory) {
    if (memory && memory == payload && ++releases > 1)
        return; // Diagnose duplicate release without aborting the fixture.
    free(memory);
}
static struct smb2_pdu *probe_allocate_pdu(struct smb2_context *context, enum smb2_command command,
                                           smb2_command_cb callback, void *data) {
    return failure == Pdu ? NULL : smb2_allocate_pdu(context, command, callback, data);
}
static struct smb2_iovec *probe_add_iovector(struct smb2_context *context,
                                             struct smb2_io_vectors *vectors, uint8_t *data,
                                             size_t size, void (*release)(void *)) {
    if ((failure == PayloadVector && data == payload) ||
        (failure == HeaderVector && data != payload)) {
        // smb2_add_iovector consumes its buffer even when admission fails.
        release(data);
        return NULL;
    }
    return smb2_add_iovector(context, vectors, data, size, release);
}
static int probe_pad_to_64bit(struct smb2_context *context, struct smb2_io_vectors *vectors) {
    return failure == Padding ? -1 : smb2_pad_to_64bit(context, vectors);
}

// Compile the same owned dependency unit with fixture-only fault boundaries.
// No production allocator or protocol implementation is replaced.
#define calloc probe_calloc
#define free probe_free
#define smb2_allocate_pdu probe_allocate_pdu
#define smb2_add_iovector probe_add_iovector
#define smb2_pad_to_64bit probe_pad_to_64bit
#include "dependencies/smb2/smb2-cmd-read.c"
#undef calloc
#undef free
#undef smb2_allocate_pdu
#undef smb2_add_iovector
#undef smb2_pad_to_64bit

static int check(enum Failure stage, const char *name, bool empty, bool with_buffer) {
    struct smb2_context *context = smb2_init_context();
    if (!context)
        return 0;
    static const char bytes[] = "read-only payload";
    payload = with_buffer ? malloc(sizeof(bytes)) : NULL;
    if (with_buffer && !payload) {
        smb2_destroy_context(context);
        return 0;
    }
    if (payload)
        memcpy(payload, bytes, sizeof(bytes));
    releases = 0;
    failure = stage;
    struct smb2_read_reply reply = {0};
    reply.data = payload;
    reply.data_length = empty ? 0 : sizeof(bytes);
    struct smb2_pdu *pdu = smb2_cmd_read_reply_async(context, &reply, NULL, NULL);
    int valid = stage == None ? pdu != NULL : pdu == NULL;
    if (pdu) {
        // The queued wire payload must stay alive until the PDU is released.
        int found = empty;
        if (!empty && releases == 0)
            for (int index = 0; index < pdu->out.niov; ++index) {
                const struct smb2_iovec *vector = &pdu->out.iov[index];
                if (vector->len == sizeof(bytes) && !memcmp(vector->buf, bytes, sizeof(bytes)))
                    found = 1;
            }
        valid &= found && releases == (unsigned)(empty && with_buffer);
        smb2_free_pdu(context, pdu);
    }
    valid &= releases == (unsigned)with_buffer;
    if (!valid)
        fprintf(stderr, "%s: reply payload released %u times\n", name, releases);
    if (!releases)
        free(payload); // Clean a reproduced leak after recording the failure.
    payload = NULL;
    smb2_destroy_context(context);
    return valid;
}

int main(void) {
    int valid = check(None, "success", false, true);
    valid &= check(Pdu, "PDU allocation", false, true);
    valid &= check(Header, "reply header allocation", false, true);
    valid &= check(HeaderVector, "header vector admission", false, true);
    valid &= check(PayloadVector, "payload vector admission", false, true);
    valid &= check(Padding, "padding allocation", false, true);
    valid &= check(None, "empty reply", true, false);
    valid &= check(None, "empty owned buffer", true, true);
    valid &= check(Pdu, "empty owned buffer PDU allocation", true, true);
    return valid ? 0 : 1;
}
