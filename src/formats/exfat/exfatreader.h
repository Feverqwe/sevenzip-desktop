// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef SEVENZIP_EXFAT_READER_H
#define SEVENZIP_EXFAT_READER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct exfat_volume;
struct exfat_timestamp {
    int64_t seconds;
    uint32_t nanoseconds;
};
struct exfat_timestamp exfat_entry_time(uint16_t date, uint16_t time, uint8_t cs, uint8_t tz);
struct exfat_source {
    void *context;
    uint64_t size;
    int (*read)(void *, uint64_t, void *, size_t);
    int (*poll)(void *);
    int (*visit_cluster)(void *, uint32_t);
};
enum exfat_read_limitations {
    EXFAT_READ_SHORT_SOURCE = 1,
    EXFAT_READ_BOOT_UNVERIFIED = 2,
    EXFAT_READ_ROOT_INCOMPLETE = 4,
    EXFAT_READ_BITMAP_UNAVAILABLE = 8,
    EXFAT_READ_ROOT_UNALLOCATED = 16,
    EXFAT_READ_LABEL_UNAVAILABLE = 32
};
struct exfat_item {
    uint16_t name[256];
    uint64_t size, valid_size;
    uint32_t cluster;
    uint16_t attributes;
    uint8_t flags;
    uint16_t create_date, create_time, modify_date, modify_time, access_date, access_time;
    uint8_t create_cs, modify_cs, create_tz, modify_tz, access_tz;
};
struct exfat_cursor {
    uint64_t position;
    uint32_t cluster, hint_cluster, hint_offset;
    uint64_t size;
    uint8_t flags;
    uint8_t incomplete;
};
int exfat_open_volume(const struct exfat_source *, struct exfat_volume **);
void exfat_close_volume(struct exfat_volume *);
uint64_t exfat_volume_size(const struct exfat_volume *);
uint16_t exfat_volume_status(const struct exfat_volume *);
int exfat_volume_free_size(const struct exfat_volume *, uint64_t *);
uint32_t exfat_volume_limitations(const struct exfat_volume *);
uint32_t exfat_volume_cluster_size(const struct exfat_volume *);
uint32_t exfat_volume_sector_size(const struct exfat_volume *);
// Owned by the volume; UTF-16 terminated at the first NUL, as in Linux's ioctl.
const uint16_t *exfat_volume_label(const struct exfat_volume *);
void exfat_root_cursor(const struct exfat_volume *, struct exfat_cursor *);
void exfat_item_cursor(const struct exfat_item *, struct exfat_cursor *);
// 1 = item, 0 = end, negative = error. Names remain UTF-16 until the Qt/7-Zip boundary.
int exfat_next_item(struct exfat_volume *, struct exfat_cursor *, struct exfat_item *);
// Read at cursor.position. A forward jump walks metadata only; a backward seek
// first resets this private cursor and its caller's cluster-visit set.
int exfat_read_item(struct exfat_volume *, const struct exfat_item *, struct exfat_cursor *, void *,
                    size_t, size_t *);
#ifdef __cplusplus
}
#endif
#endif
