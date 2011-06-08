/*
 * SYSV File System Famiy for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of sysvfs. See fs/sysv/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */

#include "sysvfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static void
detected_xenix(struct sysv_sb_info* sbi)
{
    struct buffer_head* bh1 = sbi->s_bh1;
    struct buffer_head* bh2 = sbi->s_bh2;
    struct xenix_super_block* sbd1;
    struct xenix_super_block* sbd2;

    if (bh1 != bh2)
        sbd1 = sbd2 = (struct xenix_super_block*) bh1;
    else {
        /* block size = 512, so bh1 != bh2 */
        sbd1 = (struct xenix_super_block*)bh1->b_data;
        sbd2 = (struct xenix_super_block*)(bh2->b_data - 512);
    }

    sbi->s_link_max = XENIX_LINK_MAX;
    sbi->s_fic_size = XENIX_NICINOD;
    sbi->s_flc_size = XENIX_NICFREE;
    sbi->s_sbd1 = (char*)sbd1;
    sbi->s_sbd2 = (char*)sbd2;
    sbi->s_sb_fic_count = &sbd1->s_ninode;
    sbi->s_sb_fic_inodes = &sbd1->s_inode[0];
    sbi->s_sb_total_free_inodes = &sbd2->s_tinode;
    sbi->s_bcache_count = &sbd1->s_nfree;
    sbi->s_bcache = &sbd1->s_free[0];
    sbi->s_free_blocks = &sbd2->s_tfree;
    sbi->s_sb_time = &sbd2->s_time;
    sbi->s_firstdatazone = fs16_to_host(sbi->s_bytesex, sbd1->s_isize);
    sbi->s_nzones = fs32_to_host(sbi->s_bytesex, sbd1->s_fsize);
}

static void
detected_sysv4(struct sysv_sb_info* sbi)
{
    struct sysv4_super_block* sbd;
    struct buffer_head* bh1 = sbi->s_bh1;
    struct buffer_head* bh2 = sbi->s_bh2;

    if (bh1 == bh2)
        sbd = (struct sysv4_super_block*)(bh1->b_data + BLOCK_SIZE/2);
    else
        sbd = (struct sysv4_super_block*)bh2->b_data;

    sbi->s_link_max = SYSV_LINK_MAX;
    sbi->s_fic_size = SYSV_NICINOD;
    sbi->s_flc_size = SYSV_NICFREE;
    sbi->s_sbd1 = (char*)sbd;
    sbi->s_sbd2 = (char*)sbd;
    sbi->s_sb_fic_count = &sbd->s_ninode;
    sbi->s_sb_fic_inodes = &sbd->s_inode[0];
    sbi->s_sb_total_free_inodes = &sbd->s_tinode;
    sbi->s_bcache_count = &sbd->s_nfree;
    sbi->s_bcache = &sbd->s_free[0];
    sbi->s_free_blocks = &sbd->s_tfree;
    sbi->s_sb_time = &sbd->s_time;
    sbi->s_sb_state = &sbd->s_state;
    sbi->s_firstdatazone = fs16_to_host(sbi->s_bytesex, sbd->s_isize);
    sbi->s_nzones = fs32_to_host(sbi->s_bytesex, sbd->s_fsize);
}

static void
detected_sysv2(struct sysv_sb_info* sbi)
{
    struct sysv2_super_block* sbd;
    struct buffer_head* bh1 = sbi->s_bh1;
    struct buffer_head* bh2 = sbi->s_bh2;

    if (bh1 == bh2)
        sbd = (struct sysv2_super_block*)(bh1->b_data + BLOCK_SIZE/2);
    else
        sbd = (struct sysv2_super_block*)bh2->b_data;

    sbi->s_link_max = SYSV_LINK_MAX;
    sbi->s_fic_size = SYSV_NICINOD;
    sbi->s_flc_size = SYSV_NICFREE;
    sbi->s_sbd1 = (char*)sbd;
    sbi->s_sbd2 = (char*)sbd;
    sbi->s_sb_fic_count = &sbd->s_ninode;
    sbi->s_sb_fic_inodes = &sbd->s_inode[0];
    sbi->s_sb_total_free_inodes = &sbd->s_tinode;
    sbi->s_bcache_count = &sbd->s_nfree;
    sbi->s_bcache = &sbd->s_free[0];
    sbi->s_free_blocks = &sbd->s_tfree;
    sbi->s_sb_time = &sbd->s_time;
    sbi->s_sb_state = &sbd->s_state;
    sbi->s_firstdatazone = fs16_to_host(sbi->s_bytesex, sbd->s_isize);
    sbi->s_nzones = fs32_to_host(sbi->s_bytesex, sbd->s_fsize);
}

static void
detected_coherent(struct sysv_sb_info* sbi)
{
    struct coh_super_block* sbd;
    struct buffer_head* bh1 = sbi->s_bh1;

    sbd = (struct coh_super_block*)bh1->b_data;

    sbi->s_link_max = COH_LINK_MAX;
    sbi->s_fic_size = COH_NICINOD;
    sbi->s_flc_size = COH_NICFREE;
    sbi->s_sbd1 = (char*)sbd;
    sbi->s_sbd2 = (char*)sbd;
    sbi->s_sb_fic_count = &sbd->s_ninode;
    sbi->s_sb_fic_inodes = &sbd->s_inode[0];
    sbi->s_sb_total_free_inodes = &sbd->s_tinode;
    sbi->s_bcache_count = &sbd->s_nfree;
    sbi->s_bcache = &sbd->s_free[0];
    sbi->s_free_blocks = &sbd->s_tfree;
    sbi->s_sb_time = &sbd->s_time;
    sbi->s_firstdatazone = fs16_to_host(sbi->s_bytesex, sbd->s_isize);
    sbi->s_nzones = fs32_to_host(sbi->s_bytesex, sbd->s_fsize);
}

static int
detect_xenix(struct sysv_sb_info* sbi, struct buffer_head* bh)
{
    struct xenix_super_block* sbd = (struct xenix_super_block*)bh->b_data;
    if (*(__le32*)&sbd->s_magic == cpu_to_le32(0x2b5544))
        sbi->s_bytesex = UNIXFS_FS_LITTLE;
    else if (*(__be32*)&sbd->s_magic == cpu_to_be32(0x2b5544))
        sbi->s_bytesex = UNIXFS_FS_BIG;
    else
        return 0;

    switch (fs32_to_host(sbi->s_bytesex, sbd->s_type)) {
    case 1:
        sbi->s_type = FSTYPE_XENIX;
        return 1;
    case 2:
        sbi->s_type = FSTYPE_XENIX;
        return 2;
    default:
        return 0;
    }
}

static int
detect_sysv(struct sysv_sb_info* sbi, struct buffer_head* bh)
{
    struct super_block *sb = sbi->s_sb;
    /* All relevant fields are at the same offsets in SVR2 and SVR4 */
    struct sysv4_super_block* sbd;
    u32 type;

    sbd = (struct sysv4_super_block*) (bh->b_data + BLOCK_SIZE/2);
    if (*(__le32*)&sbd->s_magic == cpu_to_le32(0xfd187e20))
        sbi->s_bytesex = UNIXFS_FS_LITTLE;
    else if (*(__be32*)&sbd->s_magic == cpu_to_be32(0xfd187e20))
        sbi->s_bytesex = UNIXFS_FS_BIG;
    else
        return 0;

    type = fs32_to_host(sbi->s_bytesex, sbd->s_type);
 
     if (fs16_to_host(sbi->s_bytesex, sbd->s_nfree) == 0xffff) {
         sbi->s_type = FSTYPE_AFS;
         sbi->s_forced_ro = 1;
         if (!(sb->s_flags & MS_RDONLY)) {
             printk("SysV FS: SCO EAFS detected, " 
                 "forcing read-only mode.\n");
         }
         return type;
     }
 
    if (fs32_to_host(sbi->s_bytesex, sbd->s_time) < JAN_1_1980) {
        /* this is likely to happen on SystemV2 FS */
        if (type > 3 || type < 1)
            return 0;
        sbi->s_type = FSTYPE_SYSV2;
        return type;
    }

    if ((type > 3 || type < 1) && (type > 0x30 || type < 0x10))
        return 0;

    /*
     * On Interactive Unix (ISC) Version 4.0/3.x s_type field = 0x10,
     * 0x20 or 0x30 indicates that symbolic links and the 14-character
     * filename limit is gone. Due to lack of information about this
     * feature read-only mode seems to be a reasonable approach... -KGB
     */

    if (type >= 0x10) {
        printk("SysV FS: can't handle long file names on, "
               "forcing read-only mode.\n");
        sbi->s_forced_ro = 1;
    }

    sbi->s_type = FSTYPE_SYSV4;
    return type >= 0x10 ? type >> 4 : type;
}

static int
detect_coherent(struct sysv_sb_info* sbi, struct buffer_head* bh)
{
    struct coh_super_block* sbd;

    sbd = (struct coh_super_block*) (bh->b_data + BLOCK_SIZE/2);
    if ((memcmp(sbd->s_fname, "noname", 6) &&
         memcmp(sbd->s_fname, "xxxxx ", 6)) ||
        (memcmp(sbd->s_fpack, "nopack", 6) &&
         memcmp(sbd->s_fpack, "xxxxx\n", 6)))
        return 0;
    sbi->s_bytesex = UNIXFS_FS_PDP;
    sbi->s_type = FSTYPE_COH;
    return 1;
}

static int
detect_sysv_odd(struct sysv_sb_info* sbi, struct buffer_head* bh)
{
    int size = detect_sysv(sbi, bh);

    return size > 2 ? 0 : size;
}

static struct {
    int block;
    int (*test)(struct sysv_sb_info*, struct buffer_head*);
} flavours[] = {
    { 1,  detect_xenix    },
    { 0,  detect_sysv     },
    { 0,  detect_coherent },
    { 9,  detect_sysv_odd },
    { 15, detect_sysv_odd },
    { 18, detect_sysv     },
};

static char *flavour_names[] = {
    [FSTYPE_XENIX] = "Xenix",
    [FSTYPE_SYSV4] = "System V",
    [FSTYPE_SYSV2] = "System V Release 2",
    [FSTYPE_COH]   = "Coherent",
    [FSTYPE_AFS]   = "AFS",
};

static void (*flavour_setup[])(struct sysv_sb_info*) = {
    [FSTYPE_XENIX] = detected_xenix,
    [FSTYPE_SYSV4] = detected_sysv4,
    [FSTYPE_SYSV2] = detected_sysv2,
    [FSTYPE_COH]   = detected_coherent,
    [FSTYPE_AFS]   = detected_sysv4,
};

static int
complete_read_super(struct super_block* sb, int silent, int size)
{
    struct sysv_sb_info* sbi = SYSV_SB(sb);
    char* found = flavour_names[sbi->s_type];
    u_char n_bits = size + 8;
    int bsize = 1 << n_bits;
    int bsize_4 = bsize >> 2;

    sbi->s_firstinodezone = 2;

    flavour_setup[sbi->s_type](sbi);
    
    sbi->s_truncate = 1;
    sbi->s_ndatazones = sbi->s_nzones - sbi->s_firstdatazone;
    sbi->s_inodes_per_block = bsize >> 6;
    sbi->s_inodes_per_block_1 = (bsize >> 6) - 1;
    sbi->s_inodes_per_block_bits = n_bits-6;
    sbi->s_ind_per_block = bsize_4;
    sbi->s_ind_per_block_2 = bsize_4 * bsize_4;
    sbi->s_toobig_block = 10 + bsize_4 * (1 + bsize_4 * (1 + bsize_4));
    sbi->s_ind_per_block_bits = n_bits - 2;

    sbi->s_ninodes = (sbi->s_firstdatazone - sbi->s_firstinodezone)
        << sbi->s_inodes_per_block_bits;

    if (!silent)
        printk("VFS: Found a %s FS (block size = %ld)\n",
               found, sb->s_blocksize);

    sb->s_magic = SYSV_MAGIC_BASE + sbi->s_type;
    /* set up enough so that it can read an inode */

    sb->s_flags |= MS_RDONLY;
    sb->s_dirt = 1;
    return 1;
}

const char*
sysv_flavor(u_int type)
{
    return flavour_names[type];
}

struct super_block*
sysv_fill_super(int fd, void* args, int silent)
{
    struct super_block* sb;
    struct buffer_head* bh1 = NULL;
    struct buffer_head* bh = NULL;
    struct sysv_sb_info* sbi;

    unsigned long blocknr;
    int size = 0, i, ret;
    
    BUILD_BUG_ON(1024 != sizeof (struct xenix_super_block));
    BUILD_BUG_ON(512 != sizeof (struct sysv4_super_block));
    BUILD_BUG_ON(512 != sizeof (struct sysv2_super_block));
    BUILD_BUG_ON(500 != sizeof (struct coh_super_block));
    BUILD_BUG_ON(64 != sizeof (struct sysv_dinode));

    sb = calloc(1, sizeof(struct super_block));
    if (!sb)
        return NULL;

    sbi = calloc(1, sizeof(struct sysv_sb_info));
    if (!sbi) {
        free(sb);
        return NULL;
    }

    sb->s_bdev = fd;
    sb->s_fs_info = sbi;
    sbi->s_sb = sb;
    sbi->s_block_base = 0;
    sb->s_fs_info = sbi;

    sb->s_blocksize = BLOCK_SIZE;
    sb->s_blocksize_bits = BLOCK_SIZE_BITS;

    if ((bh = malloc(sizeof(struct buffer_head))) == NULL)
        goto failed;

    for (i = 0; i < ARRAY_SIZE(flavours) && !size; i++) {
        blocknr = flavours[i].block;
        if ((ret = pread(fd, bh->b_data, BLOCK_SIZE,
                        (off_t)(flavours[i].block * BLOCK_SIZE))) != BLOCK_SIZE)
            continue;
        size = flavours[i].test(SYSV_SB(sb), bh);
    }

    if (!size)
        goto Eunknown;

    switch (size) {
        case 1:
            blocknr = blocknr << 1;
            sb->s_blocksize = 512;
            sb->s_blocksize_bits = blksize_bits(512);
            if ((bh1 = malloc(sizeof(struct buffer_head))) == NULL) {
                brelse(bh);
                goto failed;
            }
            ret = pread(fd, bh1->b_data, 512, (off_t)(blocknr * 512));
            ret = pread(fd, bh->b_data, 512, (off_t)((blocknr + 1) * 512));
            break;

        case 2:
            bh1 = bh;
            break;

        case 3:
            blocknr = blocknr >> 1;
            sb->s_blocksize = 2048;
            sb->s_blocksize_bits = blksize_bits(2048);
            ret = pread(fd, bh->b_data, 2048, (off_t)(blocknr * 2048));
            bh1 = bh;
            break;

        default:
            goto Ebadsize;
    }

    if (bh && bh1) {
        sbi->s_bh1 = bh1;
        sbi->s_bh2 = bh;
        sb->s_endian = sbi->s_bytesex;
        if (complete_read_super(sb, silent, size))
            return sb;
    }

    sb->s_blocksize = BLOCK_SIZE;
    sb->s_blocksize_bits = BLOCK_SIZE_BITS;

    printk("oldfs: cannot read superblock\n");

failed:
    free(sbi);
    return NULL;

Eunknown:
    if (bh) {
        if (bh1 == bh)
            bh1 = NULL;
        brelse(bh);
    }
    if (bh1)
        brelse(bh1);
    if (!silent)
        printk("VFS: unable to find oldfs superblock\n");
    goto failed;

Ebadsize:
    if (bh) {
        if (bh1 == bh)
            bh1 = NULL;
        brelse(bh);
    }
    if (bh1)
        brelse(bh1);
    if (!silent)
        printk("VFS: oldfs: unsupported block size (%dKb)\n", 1<<(size-2));
    goto failed;
}


static inline sysv_zone_t*
get_chunk(struct super_block* sb, struct buffer_head* bh)
{
    char* bh_data = (char*)bh->b_data;

    if (SYSV_SB(sb)->s_type == FSTYPE_SYSV4)
        return (sysv_zone_t*)(bh_data + 4);
    else
        return (sysv_zone_t*)(bh_data + 2);
}

unsigned long
sysv_count_free_blocks(struct super_block* sb)
{
    struct sysv_sb_info* sbi = SYSV_SB(sb);
    sysv_zone_t* blocks;
    int count, sb_count, n;
    unsigned block;

    struct buffer_head bh;

    if (sbi->s_type == FSTYPE_AFS)
        return 0;

    sb_count = fs32_to_host(sbi->s_bytesex, *sbi->s_free_blocks);

    if (0)
        goto trust_sb;

    count = 0;
    n = fs16_to_host(sbi->s_bytesex, *sbi->s_bcache_count);
    blocks = sbi->s_bcache;

    while (1) {
        sysv_zone_t zone;
        if (n > sbi->s_flc_size)
            goto E2big;
        zone = 0;
        while (n && (zone = blocks[--n]) != 0)
            count++;
        if (zone == 0)
            break;

        block = fs32_to_host(sbi->s_bytesex, zone);

        if (block < sbi->s_firstdatazone || block >= sbi->s_nzones)
            goto Einval;
        block += sbi->s_block_base;
        int ret = sb_bread_intobh(sb, block, &bh);
        if (ret != 0)
            goto Eio;
        n = fs16_to_host(sbi->s_bytesex, *(__fs16*)(bh.b_data));
        blocks = get_chunk(sb, &bh);
    }
    if (count != sb_count)
        goto Ecount;
done:
    return count;

Einval:
    printk("sysv_count_free_blocks: new block %d is not in data zone\n", block);
    goto trust_sb;
Eio:
    printk("sysv_count_free_blocks: cannot read free-list block\n");
    goto trust_sb;
E2big:
    printk("sysv_count_free_blocks: >flc_size entries in free-list block\n")
;
trust_sb:
    count = sb_count;
    goto done;
Ecount:
    printk("sysv_count_free_blocks: free block count was %d, correcting to %d",
           sb_count, count);
    goto done;
}

struct sysv_dinode*
sysv_raw_inode(struct super_block* sb, ino_t ino, struct buffer_head* bh)
{
    struct sysv_sb_info* sbi = SYSV_SB(sb);
    struct sysv_dinode* res;
    int block = sbi->s_firstinodezone + sbi->s_block_base;

    block += ((unsigned int)ino - 1) >> sbi->s_inodes_per_block_bits;
    int ret = sb_bread_intobh(sb, block, bh);
    if (ret != 0)
        return NULL;
    res = (struct sysv_dinode*)(bh->b_data);
    return res + (((unsigned int)ino - 1) & sbi->s_inodes_per_block_1);
}

unsigned long
sysv_count_free_inodes(struct super_block* sb)
{
    struct sysv_sb_info* sbi = SYSV_SB(sb);
    int ino, count, sb_count;
    struct sysv_dinode* raw_inode;

    struct buffer_head bh;

    sb_count = fs16_to_host(sbi->s_bytesex, *sbi->s_sb_total_free_inodes);

    if (0)
        goto trust_sb;

    count = 0;
    ino = SYSV_ROOT_INO+1;

    raw_inode = sysv_raw_inode(sb, ino, &bh);
    if (!raw_inode)
        goto Eio;

    while (ino <= sbi->s_ninodes) {
        if (raw_inode->di_mode == 0 && raw_inode->di_nlink == 0)
            count++;
        if ((ino++ & sbi->s_inodes_per_block_1) == 0) {
            raw_inode = sysv_raw_inode(sb, ino, &bh);
            if (!raw_inode)
                goto Eio;
        } else
            raw_inode++;
    }

    if (count != sb_count)
        goto Einval;
out:
    return count;

Einval:
    printk("sysv_count_free_inodes: free inode count was %d, correcting to %d\n",
           sb_count, count);
    goto out;

Eio:
    printk("sysv_count_free_inodes: unable to read inode table\n");
trust_sb:
    count = sb_count;
    goto out;
}

enum { DIRECT = 10, DEPTH = 4 };  /* Have triple indirect */

static int
block_to_path(struct inode* inode, long block, int offsets[DEPTH])
{
    struct super_block* sb = inode->I_sb;
    struct sysv_sb_info* sbi = SYSV_SB(sb);

    int ptrs_bits = sbi->s_ind_per_block_bits;

    unsigned long indirect_blocks = sbi->s_ind_per_block;
    unsigned long double_blocks = sbi->s_ind_per_block_2;

    int n = 0;

    if (block < 0) {
        printk("sysv_block_map: block < 0\n");
    } else if (block < DIRECT) {
        offsets[n++] = block;
    } else if ( (block -= DIRECT) < indirect_blocks) {
        offsets[n++] = DIRECT;
        offsets[n++] = block;
    } else if ((block -= indirect_blocks) < double_blocks) {
        offsets[n++] = DIRECT+1;
        offsets[n++] = block >> ptrs_bits;
        offsets[n++] = block & (indirect_blocks - 1);
    } else if (((block -= double_blocks) >> (ptrs_bits * 2)) < indirect_blocks) {
        offsets[n++] = DIRECT+2;
        offsets[n++] = block >> (ptrs_bits * 2);
        offsets[n++] = (block >> ptrs_bits) & (indirect_blocks - 1);
        offsets[n++] = block & (indirect_blocks - 1);
    } else {
        /* nothing */;
    }

    return n;
}

static inline int
block_to_host(struct sysv_sb_info* sbi, sysv_zone_t nr)
{
        return sbi->s_block_base + fs32_to_host(sbi->s_bytesex, nr);
}

typedef struct {
    sysv_zone_t* p;
    sysv_zone_t  key;
    struct buffer_head* bh;
} Indirect;

static inline void
add_chain(Indirect* p, struct buffer_head* bh, sysv_zone_t* v)
{
    p->key = *(p->p = v);
    p->bh = bh;
}

static inline int
verify_chain(Indirect* from, Indirect* to)
{
    while (from <= to && from->key == *from->p)
        from++;
    return (from > to);
}

static Indirect*
get_branch(struct inode* inode, int depth, int offsets[], Indirect chain[],
           int* err)
{
    struct super_block* sb = inode->I_sb;
    Indirect* p = chain;
    struct buffer_head* bh = 0;

    *err = 0;
    add_chain(chain, NULL, SYSV_I(inode)->i_data + *offsets);
    if (!p->key)
        goto no_block;

    while (--depth) {
        int block = block_to_host(SYSV_SB(sb), p->key);
        bh = malloc(sizeof(struct buffer_head));
        if (sb_bread_intobh(sb, block, bh) != 0)
            goto failure;
        if (!verify_chain(chain, p))
            goto changed;
        add_chain(++p, bh, (sysv_zone_t*)bh->b_data + *++offsets);
        if (!p->key)
            goto no_block;
    }

    return NULL;

changed:
    brelse(bh);
    *err = -EAGAIN;
    goto no_block;

failure:
    *err = -EIO;

no_block:
    return p;
}

int
sysv_get_block(struct inode* inode, sector_t iblock, off_t* result)
{
    *result = (off_t)0;

    int err = -EIO;
    int offsets[DEPTH];
    Indirect chain[DEPTH];
    Indirect* partial;

    struct super_block* sb = inode->I_sb;

    int depth = block_to_path(inode, iblock, offsets);

    if (depth == 0)
        goto out;

/* reread: */
    partial = get_branch(inode, depth, offsets, chain, &err);

    /* simplest case - block found, no allocation needed */
    if (!partial) {
        *result = (off_t)(block_to_host(SYSV_SB(sb), chain[depth-1].key));
        /* clean up and exit */
        partial = chain + depth - 1; /* the whole chain */
        goto cleanup;
    }

    /* Next simple case - plain lookup or failed read of indirect block */
cleanup:
    while (partial > chain) {
        brelse(partial->bh);
        partial--;
    }

out:
    return err;
}

int
sysv_get_page(struct inode* inode, sector_t index, char* pagebuf)
{
    sector_t iblock, lblock;
    unsigned int blocksize;

    blocksize = 1 << inode->I_blkbits;

    iblock = index << (PAGE_CACHE_SHIFT - inode->I_blkbits);
    lblock = (inode->I_size + blocksize - 1) >> inode->I_blkbits;

    struct super_block* sb = (struct super_block*)inode->I_sb;

    int err = 0, byte_count = 0;
    char *p = pagebuf;

    do {
        off_t phys64 = 0;
        int ret = sysv_get_block(inode, iblock, &phys64);
        if (phys64) {
            struct buffer_head bh;
            memset(&bh, 0, sizeof(bh));
            ret = sb_bread_intobh(sb, phys64, &bh);
            if (ret == 0) {
                memcpy(p, bh.b_data, blocksize);
                p += blocksize;
                byte_count += blocksize;
            } else {
                err = EIO;
                fprintf(stderr, "*** fatal error: I/O error\n");
                abort();
            }
        } else if (ret == 0) {
            memset(p, 0, blocksize);
            p += blocksize;
            byte_count += blocksize;
        } else {
            err = EIO;
            fprintf(stderr, "*** fatal error: block mapping failed\n");
            abort();
        }

        iblock++;

        if ((byte_count >= PAGE_SIZE) || (iblock >= lblock))
            break;

    } while (1);

    if (err)
        return -1;

    return 0;
}

int
sysv_next_direntry(struct inode* dir, struct unixfs_dirbuf* dirbuf,
                   off_t* offset, struct unixfs_direntry* dent)
{
    struct super_block* sb = dir->I_sb;
    struct sysv_sb_info* sbi = SYSV_SB(sb);
    unsigned long start, n;
    unsigned long npages = sysv_dir_pages(dir);
    struct sysv_dir_entry* de;
    char *dirpagebuf = dirbuf->data;

    *offset = (*offset + SYSV_DIRSIZE-1) & ~(SYSV_DIRSIZE-1);

    if (*offset >= dir->I_size)
        return -1;

    if (npages == 0)
        return -1;

    start = *offset >> PAGE_CACHE_SHIFT; /* which page from offset */

    if (start >= npages)
        return -1;
    n = start;

    if (!dirbuf->flags.initialized || (*offset & ((PAGE_SIZE - 1))) == 0) {
        int ret = sysv_get_page(dir, n, dirpagebuf);
        if (ret)
            return ret;
        dirbuf->flags.initialized = 1;
    }

    de = (struct sysv_dir_entry*)
             ((char*)dirpagebuf + (*offset & (PAGE_SIZE - 1)));

    dent->ino = fs16_to_host(sbi->s_bytesex, de->inode);
    size_t nl = strlen(de->name);
    nl = min(nl, SYSV_NAMELEN);
    memcpy(dent->name, de->name, nl);
    dent->name[nl] = '\0';

    *offset += SYSV_DIRSIZE;

    return 0;
}
