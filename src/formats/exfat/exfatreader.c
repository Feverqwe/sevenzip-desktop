// SPDX-License-Identifier: GPL-2.0-or-later
// Stream/lifetime adapter for the Linux exFAT read modules.
#include "formats/exfat/exfatreader.h"
#include "formats/exfat/exfat_fs.h"

struct exfat_volume {
    struct super_block sb;
    struct exfat_sb_info info;
    struct exfat_chain root;
    struct exfat_uni_name label;
    uint32_t limitations;
    bool free_known;
    int (*visit_cluster)(void *, uint32_t);
};

struct exfat_timestamp exfat_entry_time(uint16_t date, uint16_t time, uint8_t cs, uint8_t tz) {
    struct timespec64 result;
    exfat_get_entry_time(NULL, &result, tz, cpu_to_le16(time), cpu_to_le16(date), cs);
    return (struct exfat_timestamp){result.tv_sec, (uint32_t)result.tv_nsec};
}

struct buffer_head *sb_bread(struct super_block *sb, sector_t sector) {
    if (sb->poll && sb->poll(sb->context))
        return NULL;
    if (sector > (UINT64_MAX >> sb->s_blocksize_bits))
        return NULL;
    uint64_t offset = sector << sb->s_blocksize_bits;
    uint64_t available = min(sb->image_size, sb->source_size);
    if (offset > available || sb->s_blocksize > available - offset)
        return NULL;
    struct buffer_head *bh = malloc(sizeof(*bh) + sb->s_blocksize);
    if (!bh) {
        sb->allocation_error = true;
        return NULL;
    }
    bh->b_data = (char *)(bh + 1);
    bh->b_blocknr = sector;
    if (sb->read(sb->context, offset, bh->b_data, sb->s_blocksize)) {
        free(bh);
        return NULL;
    }
    return bh;
}

void brelse(struct buffer_head *bh) { free(bh); }

int sb_set_blocksize(struct super_block *sb, unsigned int size) {
    if (!is_power_of_2(size) || size < 512 || size > 4096)
        return 0;
    sb->s_blocksize = size;
    sb->s_blocksize_bits = 0;
    while ((1u << sb->s_blocksize_bits) < size)
        sb->s_blocksize_bits++;
    return size;
}

int exfat_utf16_to_nls(struct super_block *sb, struct exfat_uni_name *name,
                        unsigned char *buffer, int length) {
    (void)sb;
    if (length < (int)sizeof(name->name))
        return -EINVAL;
    memcpy(buffer, name->name, sizeof(name->name));
    return 0;
}

void exfat_close_volume(struct exfat_volume *volume) {
    if (!volume)
        return;
    if (volume->info.vol_amap)
        exfat_free_bitmap(&volume->info);
    brelse(volume->info.boot_bh);
    free(volume);
}

static int exfat_partial_error(struct exfat_volume *v, int error, uint32_t limitation) {
    if (!error)
        return 0;
    if (error == -ENOMEM || v->sb.allocation_error)
        return -ENOMEM;
    if (v->sb.poll && v->sb.poll(v->sb.context))
        return -EIO;
    v->limitations |= limitation;
    return 0;
}

int exfat_open_volume(const struct exfat_source *source, struct exfat_volume **result) {
    *result = NULL;
    struct exfat_volume *v = calloc(1, sizeof(*v));
    if (!v)
        return -ENOMEM;
    v->sb.s_fs_info = &v->info;
    v->sb.image_size = source->size;
    v->sb.source_size = source->size;
    v->visit_cluster = source->visit_cluster;
    v->sb.context = source->context;
    v->sb.read = source->read;
    v->sb.poll = source->poll;
    int error = exfat_read_boot_sector(&v->sb);
    if (!error) {
        error = exfat_partial_error(v, exfat_verify_boot_region(&v->sb), EXFAT_READ_BOOT_UNVERIFIED);
        if (v->sb.boot_warning)
            v->limitations |= EXFAT_READ_BOOT_UNVERIFIED;
    }
    if (!error) {
        // The kernel relies on the block device and VFS for these bounds.
        // A seekable archive stream must establish them explicitly.
        struct exfat_sb_info *sbi = &v->info;
        const struct boot_sector *boot = (const void *)sbi->boot_bh->b_data;
        uint64_t clusters = le32_to_cpu(boot->clu_count);
        if (!clusters || clusters > EXFAT_MAX_NUM_CLUSTER ||
            !is_valid_cluster(sbi, sbi->root_dir) ||
            sbi->FAT1_start_sector < 24 ||
            sbi->num_sectors > (UINT64_MAX >> v->sb.s_blocksize_bits) ||
            sbi->data_start_sector > sbi->num_sectors ||
            clusters > ((sbi->num_sectors - sbi->data_start_sector) >> sbi->sect_per_clus_bits) ||
            (boot->num_fats == 1 && sbi->active_fat) ||
            boot->fs_revision[1] != 1)
            error = -EINVAL;
        else {
            v->sb.image_size = sbi->num_sectors << v->sb.s_blocksize_bits;
            if (source->size < v->sb.image_size)
                v->limitations |= EXFAT_READ_SHORT_SOURCE;
        }
    }
    if (!error) {
        exfat_chain_set(&v->root, v->info.root_dir, 0, ALLOC_FAT_CHAIN);
        v->sb.visit_cluster = source->visit_cluster;
        error = exfat_partial_error(v, exfat_count_num_clusters(&v->sb, &v->root, &v->root.size),
                                    EXFAT_READ_ROOT_INCOMPLETE);
        v->sb.visit_cluster = NULL;
        v->sb.root_clusters = v->root.size;
    }
    if (!error) {
        error = exfat_partial_error(v, exfat_load_bitmap(&v->sb), EXFAT_READ_BITMAP_UNAVAILABLE);
        if (!error && v->info.vol_amap) {
            error = exfat_partial_error(v, exfat_count_used_clusters(&v->sb, &v->info.used_clusters),
                                        EXFAT_READ_BITMAP_UNAVAILABLE);
            v->free_known = !error && !(v->limitations & (EXFAT_READ_BITMAP_UNAVAILABLE |
                EXFAT_READ_BOOT_UNVERIFIED | EXFAT_READ_ROOT_INCOMPLETE));
            if (!exfat_test_bitmap(&v->sb, v->info.root_dir)) {
                v->sb.root_unallocated = true;
                v->limitations |= EXFAT_READ_ROOT_UNALLOCATED;
                v->free_known = false;
            }
        }
    }
    if (!error) {
        error = exfat_partial_error(v, exfat_read_volume_label(&v->sb, &v->label), EXFAT_READ_LABEL_UNAVAILABLE);
        if (v->limitations & EXFAT_READ_LABEL_UNAVAILABLE)
            memset(&v->label, 0, sizeof(v->label));
    }
    if (error) {
        exfat_close_volume(v);
        return error;
    }
    *result = v;
    return 0;
}

uint64_t exfat_volume_size(const struct exfat_volume *v) { return v->sb.image_size; }
uint16_t exfat_volume_status(const struct exfat_volume *v) {
    return v->info.vol_flags_persistent;
}
uint32_t exfat_volume_limitations(const struct exfat_volume *v) { return v->limitations; }
int exfat_volume_free_size(const struct exfat_volume *v, uint64_t *size) {
    if (!v->free_known)
        return -EIO;
    // exfat_statfs: data clusters exclude the two reserved cluster numbers.
    *size = (uint64_t)(EXFAT_DATA_CLUSTER_COUNT(&v->info) - v->info.used_clusters)
           << v->info.cluster_size_bits;
    return 0;
}
uint32_t exfat_volume_cluster_size(const struct exfat_volume *v) { return v->info.cluster_size; }
uint32_t exfat_volume_sector_size(const struct exfat_volume *v) { return v->sb.s_blocksize; }
const uint16_t *exfat_volume_label(const struct exfat_volume *v) { return v->label.name; }

void exfat_root_cursor(const struct exfat_volume *v, struct exfat_cursor *c) {
    memset(c, 0, sizeof(*c));
    c->cluster = v->root.dir;
    c->size = (uint64_t)v->root.size << v->info.cluster_size_bits;
    c->flags = ALLOC_FAT_CHAIN;
    c->hint_offset = EXFAT_EOF_CLUSTER;
    c->incomplete = !!(v->limitations & EXFAT_READ_ROOT_INCOMPLETE);
}

void exfat_item_cursor(const struct exfat_item *item, struct exfat_cursor *c) {
    memset(c, 0, sizeof(*c));
    c->cluster = item->cluster;
    c->size = item->size;
    c->flags = item->flags;
    c->hint_offset = EXFAT_EOF_CLUSTER;
}

int exfat_next_item(struct exfat_volume *v, struct exfat_cursor *c, struct exfat_item *item) {
    if (c->position >= c->size)
        return 0;
    struct inode inode = {0};
    inode.i_sb = &v->sb;
    inode.i_size = c->size;
    inode.info.type = TYPE_DIR;
    inode.info.start_clu = c->cluster;
    inode.info.flags = c->flags;
    inode.info.hint_bmap.off = c->hint_offset;
    inode.info.hint_bmap.clu = c->hint_cluster;
    // The filesystem filename stays UTF-16; no locale-dependent conversion.
    unsigned short name[MAX_NAME_LENGTH + 3] = {0};
    struct exfat_dir_entry info = {0};
    info.namebuf.lfn = (char *)name;
    info.namebuf.lfnbuf_len = sizeof(name);
    loff_t position = c->position;
    v->sb.directory_error = false;
    int error = exfat_readdir(&inode, &position, &info);
    if (v->sb.directory_error)
        c->incomplete = 1;
    if (error)
        return error;
    if (!name[0])
        return 0;
    if ((uint64_t)position <= c->position || (uint64_t)position > c->size)
        return -EIO;
    c->position = position;
    c->hint_offset = inode.info.hint_bmap.off;
    c->hint_cluster = inode.info.hint_bmap.clu;
    struct exfat_entry_set_cache es;
    error = exfat_get_dentry_set(&es, &v->sb, &info.dir, info.entry, ES_ALL_ENTRIES);
    if (error)
        return error;
    const struct exfat_dentry *ep = exfat_get_dentry_cached(&es, ES_IDX_FILE);
    const struct exfat_dentry *stream = exfat_get_dentry_cached(&es, ES_IDX_STREAM);
    memset(item, 0, sizeof(*item));
    memcpy(item->name, name, sizeof(item->name));
    // Metadata assignments follow exfat_find() in Linux fs/exfat/namei.c.
    item->size = le64_to_cpu(stream->dentry.stream.size);
    item->valid_size = le64_to_cpu(stream->dentry.stream.valid_size);
    item->cluster = le32_to_cpu(stream->dentry.stream.start_clu);
    item->flags = stream->dentry.stream.flags;
    item->attributes = info.attr;
    item->create_date = le16_to_cpu(ep->dentry.file.create_date);
    item->create_time = le16_to_cpu(ep->dentry.file.create_time);
    item->modify_date = le16_to_cpu(ep->dentry.file.modify_date);
    item->modify_time = le16_to_cpu(ep->dentry.file.modify_time);
    item->access_date = le16_to_cpu(ep->dentry.file.access_date);
    item->access_time = le16_to_cpu(ep->dentry.file.access_time);
    item->create_cs = ep->dentry.file.create_time_cs;
    item->modify_cs = ep->dentry.file.modify_time_cs;
    item->create_tz = ep->dentry.file.create_tz;
    item->modify_tz = ep->dentry.file.modify_tz;
    item->access_tz = ep->dentry.file.access_tz;
    unsigned int name_len = stream->dentry.stream.name_len;
    unsigned int entries = es.num_entries;
    exfat_put_dentry_set(&es, false);
    if (!name_len ||
        EXFAT_FILENAME_ENTRY_NUM(name_len) + 2 > entries ||
        item->name[name_len] || item->valid_size > item->size ||
        item->size > v->sb.image_size ||
        (item->size && !is_valid_cluster(&v->info, item->cluster)) ||
        (item->size && item->flags != ALLOC_FAT_CHAIN && item->flags != ALLOC_NO_FAT_CHAIN))
        return -EIO;
    for (unsigned int i = 0; i < name_len; i++)
        if (!item->name[i])
            return -EIO;
    if (item->flags == ALLOC_NO_FAT_CHAIN && item->size &&
        exfat_bytes_to_cluster_round_up(&v->info, item->size) > v->info.num_clusters - item->cluster)
        return -EIO;
    if ((item->attributes & EXFAT_ATTR_SUBDIR) &&
        ((item->size & (v->info.cluster_size - 1)) || item->valid_size != item->size))
        return -EIO;
    if (!item->size) {
        item->cluster = EXFAT_EOF_CLUSTER;
        item->flags = ALLOC_NO_FAT_CHAIN;
    }
    return 1;
}

/* First-cluster mapping from fs/exfat/cache.c:exfat_get_cluster.
 * Written 1992,1993 by Werner Almesberger.
 * Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 * The per-stream hint replaces the VFS inode LRU; preserve the cached FAT-sector walk and add
 * userspace cycle validation for every traversed cluster, including seeks. */
static int exfat_map_item_cluster(struct exfat_volume *v, const struct exfat_item *item,
                                  struct exfat_cursor *c, unsigned int offset) {
    if (c->hint_offset == EXFAT_EOF_CLUSTER) {
        c->hint_offset = 0;
        c->hint_cluster = item->cluster;
        if (v->visit_cluster && v->visit_cluster(v->sb.context, c->hint_cluster))
            return -EIO;
    }
    if (offset < c->hint_offset)
        return -EINVAL;
    if (item->flags == ALLOC_NO_FAT_CHAIN) {
        if (offset != c->hint_offset) {
            c->hint_cluster += offset - c->hint_offset;
            c->hint_offset = offset;
            if (v->visit_cluster && v->visit_cluster(v->sb.context, c->hint_cluster))
                return -EIO;
        }
        return 0;
    }
    struct buffer_head *bh = NULL;
    while (c->hint_offset < offset) {
        unsigned int content;
        if (exfat_ent_get(&v->sb, c->hint_cluster, &content, &bh) || content == EXFAT_EOF_CLUSTER ||
            (v->visit_cluster && v->visit_cluster(v->sb.context, content))) {
            brelse(bh);
            return v->sb.allocation_error ? -ENOMEM : -EIO;
        }
        c->hint_cluster = content;
        c->hint_offset++;
    }
    brelse(bh);
    return 0;
}

int exfat_read_item(struct exfat_volume *v, const struct exfat_item *item, struct exfat_cursor *c,
                    void *data, size_t size, size_t *processed) {
    *processed = 0;
    if (c->position >= item->size)
        return 0;
    if (v->sb.poll && v->sb.poll(v->sb.context))
        return -EIO;
    size = (size_t)min_t(uint64_t, size, item->size - c->position);
    // Linux iomap.c treats bytes beyond valid_size as unwritten, never disk data.
    if (c->position >= item->valid_size) {
        memset(data, 0, size);
    } else {
        unsigned int offset = exfat_bytes_to_cluster(&v->info, c->position);
        int mapped = exfat_map_item_cluster(v, item, c, offset);
        if (mapped)
            return mapped;
        if (!is_valid_cluster(&v->info, c->hint_cluster) ||
            !exfat_test_bitmap(&v->sb, c->hint_cluster))
            return -EIO;
        uint64_t cluster_offset = exfat_cluster_offset(&v->info, c->position);
        size = min_t(uint64_t, size, v->info.cluster_size - cluster_offset);
        size = min_t(uint64_t, size, item->valid_size - c->position);
        uint64_t physical = exfat_cluster_to_phys_bytes(&v->info, c->hint_cluster) + cluster_offset;
        uint64_t available = min(v->sb.image_size, v->sb.source_size);
        if (physical >= available)
            return -EIO;
        size = min_t(uint64_t, size, available - physical);
        if (!size || v->sb.read(v->sb.context, physical, data, size))
            return -EIO;
    }
    c->position += size;
    *processed = size;
    return 0;
}
