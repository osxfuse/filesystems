/*
 * Minix File System Famiy for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of the minix file systems. See fs/minix/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */

#include "minixfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static unsigned long count_free(struct buffer_head*[], unsigned, __u32);
static unsigned long minix_count_free_blocks(struct minix_sb_info*);
static struct minix_inode*  minix_V1_raw_inode(struct super_block*, ino_t,
                                               struct buffer_head**);
static struct minix2_inode* minix_V2_raw_inode(struct super_block*, ino_t,
                                               struct buffer_head**);
static unsigned long minix_count_free_inodes(struct minix_sb_info*);
static int           minix_iget_v1(struct super_block*, struct inode*);
static int           minix_iget_v2(struct super_block*, struct inode*);
static unsigned      minix_last_byte(struct inode*, unsigned long);
static int           minix_get_dirpage(struct inode*, sector_t, char*);
static ino_t         minix_find_entry(struct inode*, const char*);

extern int           minix_get_block_v1(struct inode*, sector_t, off_t*);
extern int           minix_get_block_v2(struct inode*, sector_t, off_t*);

static const int nibblemap[] = { 4,3,3,2,3,2,2,1,3,2,2,1,2,1,1,0 };

static unsigned long
count_free(struct buffer_head* map[], unsigned numblocks, __u32 numbits)
{
    unsigned i, j, sum = 0;
    struct buffer_head* bh;
  
    for (i = 0; i < numblocks - 1; i++) {
        if (!(bh = map[i])) 
            return 0;
        for (j = 0; j < bh->b_size; j++)
            sum += nibblemap[bh->b_data[j] & 0xf]
                + nibblemap[(bh->b_data[j] >> 4) & 0xf];
    }

    if (numblocks == 0 || !(bh = map[numblocks - 1]))
        return 0;

    i = ((numbits - (numblocks - 1) * bh->b_size * 8) / 16) * 2;
    for (j = 0; j < i; j++) {
        sum += nibblemap[bh->b_data[j] & 0xf]
            + nibblemap[(bh->b_data[j] >> 4) & 0xf];
    }

    i = numbits % 16;
    if (i != 0) {
        i = *(__u16*)(&bh->b_data[j]) | ~((1 << i) - 1);
        sum += nibblemap[i & 0xf] + nibblemap[(i >> 4) & 0xf];
        sum += nibblemap[(i >> 8) & 0xf] + nibblemap[(i >> 12) & 0xf];
    }
    return(sum);
}

static unsigned long
minix_count_free_blocks(struct minix_sb_info* sbi)
{
    return (count_free(sbi->s_zmap, sbi->s_zmap_blocks,
            sbi->s_nzones - sbi->s_firstdatazone + 1) << sbi->s_log_zone_size);
}

struct minix_inode*
minix_V1_raw_inode(struct super_block* sb, ino_t ino, struct buffer_head** bh)
{
    int block;
    struct minix_sb_info* sbi = minix_sb(sb);
    struct minix_inode* p;

    if (!ino || ino > sbi->s_ninodes) {
        printk("Bad inode number on dev %s: %ld is out of range\n",
               sb->s_id, (long)ino);
        return NULL;
    }

    ino--;
    block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
                ino / MINIX_INODES_PER_BLOCK;
    int ret = sb_bread_intobh(sb, block, *bh);
    if (ret != 0) {
        printk("Unable to read inode block\n");
        return NULL;
    }
    p = (void *)(*bh)->b_data;
    return p + ino % MINIX_INODES_PER_BLOCK;
}

struct minix2_inode*
minix_V2_raw_inode(struct super_block* sb, ino_t ino, struct buffer_head** bh)
{
    int block;
    struct minix_sb_info* sbi = minix_sb(sb);
    struct minix2_inode* p;
    int minix2_inodes_per_block = sb->s_blocksize / sizeof(struct minix2_inode);

    if (!ino || ino > sbi->s_ninodes) {
        printk("Bad inode number on dev %s: %ld is out of range\n",
               sb->s_id, (long)ino);
        return NULL;
    }
    ino--;
    block = 2 + sbi->s_imap_blocks + sbi->s_zmap_blocks +
         ino / minix2_inodes_per_block;
    int ret = sb_bread_intobh(sb, block, *bh);
    if (ret != 0) {
        printk("Unable to read inode block\n");
        return NULL;
    }
    p = (void *)(*bh)->b_data;
    return p + ino % minix2_inodes_per_block;
}

static unsigned long
minix_count_free_inodes(struct minix_sb_info* sbi)
{
    return count_free(sbi->s_imap, sbi->s_imap_blocks, sbi->s_ninodes + 1);
}

static int
minix_iget_v1(struct super_block* sb, struct inode* inode)
{
    struct buffer_head _bh;
    struct buffer_head* bh = &_bh;;
    bh->b_flags.dynamic = 0;
    struct minix_inode* raw_inode;
    struct minix_inode_info* minix_inode = minix_i(inode);
    int i;

    raw_inode = minix_V1_raw_inode(inode->I_sb, inode->I_ino, &bh);
    if (!raw_inode)
        return -1;

    inode->I_mode = raw_inode->di_mode;
    inode->I_uid = (uid_t)raw_inode->di_uid;
    inode->I_gid = (gid_t)raw_inode->di_gid;
    inode->I_nlink = raw_inode->di_nlinks;
    inode->I_size = raw_inode->di_size;
    inode->I_mtime.tv_sec = inode->I_atime.tv_sec = inode->I_ctime.tv_sec =
        raw_inode->di_time;
    inode->I_mtime.tv_nsec = 0;
    inode->I_atime.tv_nsec = 0;
    inode->I_ctime.tv_nsec = 0;
    inode->I_blocks = 0;
    for (i = 0; i < 9; i++)
        minix_inode->u.i1_data[i] = raw_inode->di_zone[i];
    if (S_ISCHR(inode->I_mode) || S_ISBLK(inode->I_mode))
        inode->I_rdev = old_decode_dev(raw_inode->di_zone[0]);
    minix_inode->i_dir_start_lookup = 0;
    brelse(bh);
    return 0; 
}

static int
minix_iget_v2(struct super_block* sb, struct inode* inode)
{
    struct buffer_head _bh;
    struct buffer_head* bh = &_bh;;
    bh->b_flags.dynamic = 0;
    struct minix2_inode* raw_inode;
    struct minix_inode_info* minix_inode = minix_i(inode);
    int i;

    raw_inode = minix_V2_raw_inode(inode->I_sb, inode->I_ino, &bh);
    if (!raw_inode)
        return -1;

    inode->I_mode = raw_inode->di_mode;
    inode->I_uid = (uid_t)raw_inode->di_uid;
    inode->I_gid = (gid_t)raw_inode->di_gid;
    inode->I_nlink = raw_inode->di_nlinks;
    inode->I_size = raw_inode->di_size;
    inode->I_mtime.tv_sec = raw_inode->di_mtime;
    inode->I_atime.tv_sec = raw_inode->di_atime;
    inode->I_ctime.tv_sec = raw_inode->di_ctime;
    inode->I_mtime.tv_nsec = 0;
    inode->I_atime.tv_nsec = 0;
    inode->I_ctime.tv_nsec = 0;
    inode->I_blocks = 0;
    for (i = 0; i < 10; i++)
        minix_inode->u.i2_data[i] = raw_inode->di_zone[i];
    if (S_ISCHR(inode->I_mode) || S_ISBLK(inode->I_mode))
        inode->I_rdev = old_decode_dev(raw_inode->di_zone[0]);
    minix_inode->i_dir_start_lookup = 0;
    brelse(bh);
    return 0;
}

static unsigned
minix_last_byte(struct inode* inode, unsigned long page_nr)
{
    unsigned last_byte = PAGE_CACHE_SIZE;

    if (page_nr == (inode->I_size >> PAGE_CACHE_SHIFT))
        last_byte = inode->I_size & (PAGE_CACHE_SIZE - 1);

    return last_byte;
}

static int
minix_get_dirpage(struct inode* dir, sector_t index, char* pagebuf)
{
    return minixfs_get_page(dir, index, pagebuf);
}

static inline void* minix_next_entry(void* de, struct minix_sb_info* sbi)
{
    return (void*)((char*)de + sbi->s_dirsize);
}

static ino_t
minix_find_entry(struct inode* dir, const char* name)
{
    struct super_block* sb = dir->I_sb;
    struct minix_sb_info* sbi = minix_sb(sb);
    int namelen = strlen(name);
    unsigned long n;
    unsigned long npages = minix_dir_pages(dir);
    char page[PAGE_SIZE];
    char* p; 

    ino_t test = 0, result = 0;

    char* namx;

    for (n = 0; n < npages; n++) {
        char* kaddr;
        char* limit;
        if (minix_get_dirpage(dir, n, page) == 0) {
            kaddr = page;
            limit = kaddr + minix_last_byte(dir, n) - sbi->s_dirsize;
            for (p = kaddr; p <= limit; p = minix_next_entry(p, sbi)) {
                if (sbi->s_version == MINIX_V3) {
                    minix3_dirent* de3 = (minix3_dirent*)p;
                    namx = de3->name;
                    test = de3->inode;
                } else {
                    minix_dirent* de = (minix_dirent*)p;
                    namx = de->name;
                    test = de->inode;
                }
                if (!test)
                    continue;
                if (minix_namecompare(namelen, sbi->s_namelen, name, namx)) {
                    result = test;
                    goto found;
                }
            }
        }
    }

found:

    return result;
}

struct super_block*
minixfs_fill_super(int fd, void* data, int silent)
{
    struct super_block* sb = NULL;
    struct minix_sb_info *sbi = NULL;
    struct minix_super_block* ms = NULL;
    struct minix3_super_block* m3s = NULL;

    unsigned long i, block;
    int ret = -EINVAL;

    struct buffer_head** map;
    struct buffer_head _bh;
    struct buffer_head* bh = &_bh;
    bh->b_flags.dynamic = 0;

    sb = calloc(1, sizeof(struct super_block));
    if (!sb)
        return NULL;

    sb->s_bdev = fd;
    sb->s_flags |= MS_RDONLY;

    sbi = kzalloc(sizeof(struct minix_sb_info), GFP_KERNEL);
    if (!sbi)
        goto failed_nomem;

    sb->s_fs_info = sbi;

    BUILD_BUG_ON(32 != sizeof (struct minix_inode));
    BUILD_BUG_ON(64 != sizeof(struct minix2_inode));

    sb->s_blocksize = BLOCK_SIZE;
    sb->s_blocksize_bits = BLOCK_SIZE_BITS;

    ret = sb_bread_intobh(sb, 1, bh);
    if (ret != 0)
        goto out_bad_sb;

    ms = (struct minix_super_block*)bh->b_data;
    sbi->s_ms = ms;
    sbi->s_sbh = bh;
    sbi->s_mount_state = ms->s_state;
    sbi->s_ninodes = ms->s_ninodes;
    sbi->s_nzones = ms->s_nzones;
    sbi->s_imap_blocks = ms->s_imap_blocks;
    sbi->s_zmap_blocks = ms->s_zmap_blocks;
    sbi->s_firstdatazone = ms->s_firstdatazone;
    sbi->s_log_zone_size = ms->s_log_zone_size;
    sbi->s_max_size = ms->s_max_size;
    sb->s_magic = ms->s_magic;
    if (sb->s_magic == MINIX_SUPER_MAGIC) {
        sbi->s_version = MINIX_V1;
        sbi->s_dirsize = 16;
        sbi->s_namelen = 14;
        sbi->s_link_max = MINIX_LINK_MAX;
    } else if (sb->s_magic == MINIX_SUPER_MAGIC2) {
        sbi->s_version = MINIX_V1;
        sbi->s_dirsize = 32;
        sbi->s_namelen = 30;
        sbi->s_link_max = MINIX_LINK_MAX;
    } else if (sb->s_magic == MINIX2_SUPER_MAGIC) {
        sbi->s_version = MINIX_V2;
        sbi->s_nzones = ms->s_zones;
        sbi->s_dirsize = 16;
        sbi->s_namelen = 14;
        sbi->s_link_max = MINIX2_LINK_MAX;
    } else if (sb->s_magic == MINIX2_SUPER_MAGIC2) {
        sbi->s_version = MINIX_V2;
        sbi->s_nzones = ms->s_zones;
        sbi->s_dirsize = 32;
        sbi->s_namelen = 30;
        sbi->s_link_max = MINIX2_LINK_MAX;
    } else if ( *(__u16 *)(bh->b_data + 24) == MINIX3_SUPER_MAGIC) {
        m3s = (struct minix3_super_block *) bh->b_data;
        sb->s_magic = m3s->s_magic;
        sbi->s_imap_blocks = m3s->s_imap_blocks;
        sbi->s_zmap_blocks = m3s->s_zmap_blocks;
        sbi->s_firstdatazone = m3s->s_firstdatazone;
        sbi->s_log_zone_size = m3s->s_log_zone_size;
        sbi->s_max_size = m3s->s_max_size;
        sbi->s_ninodes = m3s->s_ninodes;
        sbi->s_nzones = m3s->s_zones;
        sbi->s_dirsize = 64;
        sbi->s_namelen = 60;
        sbi->s_version = MINIX_V3;
        sbi->s_link_max = MINIX2_LINK_MAX;
        sbi->s_mount_state = MINIX_VALID_FS;
        sb->s_blocksize = m3s->s_blocksize;
        sb->s_blocksize_bits = m3s->s_blocksize;
    } else
        goto out_no_fs;

    /*
     * Allocate the buffer map to keep the superblock small.
     */
    if (sbi->s_imap_blocks == 0 || sbi->s_zmap_blocks == 0)
        goto out_illegal_sb;
    i = (sbi->s_imap_blocks + sbi->s_zmap_blocks) * sizeof(bh);
    map = kzalloc(i, GFP_KERNEL);
    if (!map)
        goto out_no_map;
    sbi->s_imap = &map[0];
    sbi->s_zmap = &map[sbi->s_imap_blocks];

    block=2;
    for (i=0 ; i < sbi->s_imap_blocks ; i++) {
        if (!(sbi->s_imap[i] = sb_bread(sb, block)))
            goto out_no_bitmap;
            block++;
    }
    for (i=0 ; i < sbi->s_zmap_blocks ; i++) {
        if (!(sbi->s_zmap[i] = sb_bread(sb, block)))
            goto out_no_bitmap;
        block++;
    }

    minix_set_bit(0, sbi->s_imap[0]->b_data);
    minix_set_bit(0, sbi->s_zmap[0]->b_data);

    /* read the root inode */

    if (!(sbi->s_mount_state & MINIX_VALID_FS))
        printk("MINIX-fs: mounting unchecked file system, "
               "running fsck is recommended\n");
    else if (sbi->s_mount_state & MINIX_ERROR_FS)
        printk("MINIX-fs: mounting file system with errors, "
               "running fsck is recommended\n");

    return sb;

out_no_bitmap:
    printk("MINIX-fs: bad superblock or unable to read bitmaps\n");

/* out_freemap: */
    for (i = 0; i < sbi->s_imap_blocks; i++)
        brelse(sbi->s_imap[i]);

    for (i = 0; i < sbi->s_zmap_blocks; i++)
        brelse(sbi->s_zmap[i]);

    kfree(sbi->s_imap);

    goto out_release;

out_no_map:
    ret = -ENOMEM;
    if (!silent)
        printk("MINIX-fs: can't allocate map\n");
    goto out_release;

out_illegal_sb:
    if (!silent)
        printk("MINIX-fs: bad superblock\n");
    goto out_release;

out_no_fs:
    if (!silent)
        printk("VFS: Can't find a Minix filesystem V1 | V2 | V3\n");

out_release:
    brelse(bh);
    goto out;

/*out_bad_hblock:*/
    printk("MINIX-fs: blocksize too small for device\n");
    goto out;

out_bad_sb:
    printk("MINIX-fs: unable to read superblock\n");

out:
failed_nomem:

    if (sb)
        free(sb);
    if (sbi)
        free(sbi);

    return NULL;
}

int
minixfs_statvfs(struct super_block* sb, struct statvfs* buf)
{
    struct minix_sb_info* sbi = minix_sb(sb);

    buf->f_bsize   = sb->s_blocksize;
    buf->f_frsize  = sb->s_blocksize;
    buf->f_blocks  =
        (sbi->s_nzones - sbi->s_firstdatazone) << sbi->s_log_zone_size;
    buf->f_bfree   = minix_count_free_blocks(sbi);
    buf->f_bavail  = buf->f_bfree;
    buf->f_files   = sbi->s_ninodes;
    buf->f_ffree   = minix_count_free_inodes(sbi);
    buf->f_namemax = sbi->s_namelen;

    return 0;
}

int
minixfs_iget(struct super_block* sb, struct inode* inode)
{
    if (INODE_VERSION(inode) == MINIX_V1)
        return minix_iget_v1(sb, inode);
    else
        return minix_iget_v2(sb, inode);
}

ino_t
minix_inode_by_name(struct inode* dir, const char* name)
{
    return minix_find_entry(dir, name);
}

int
minixfs_next_direntry(struct inode* dir, struct unixfs_dirbuf* dirbuf,
                      off_t* offset, struct unixfs_direntry* dent)
{
    struct super_block* sb = dir->I_sb;
    struct minix_sb_info* sbi = minix_sb(sb);
    unsigned long start, n;
    unsigned long npages = minix_dir_pages(dir);
    char *dirpagebuf = dirbuf->data;

    unsigned chunk_size = sbi->s_dirsize;
    *offset = (*offset + chunk_size - 1) & ~(chunk_size - 1);

    if (*offset >= dir->I_size)
        return -1;

    if (npages == 0)
        return -1;

    start = *offset >> PAGE_CACHE_SHIFT; /* which page from offset */

    if (start >= npages)
        return -1;
    n = start;

    if (!dirbuf->flags.initialized || (*offset & ((PAGE_SIZE - 1))) == 0) {
        int ret = minixfs_get_page(dir, n, dirpagebuf);
        if (ret)
            return ret;
        dirbuf->flags.initialized = 1;
    }

    char* name = NULL;

    if (sbi->s_version == MINIX_V3) {
        minix3_dirent* de3 =
            (minix3_dirent*)((char*)dirpagebuf + (*offset & (PAGE_SIZE - 1)));
        dent->ino = de3->inode;
        name = de3->name;
    } else {
        minix_dirent* de =
            (minix_dirent*)((char*)dirpagebuf + (*offset & (PAGE_SIZE - 1)));
        dent->ino = de->inode;
        name = de->name;
    }

    if (dent->ino) {
        size_t nl = min(strlen(name), sbi->s_namelen);
        memcpy(dent->name, name, nl);
        dent->name[nl] = '\0';
    }

    *offset += chunk_size;

    return 0;
}

int
minixfs_get_block(struct inode* inode, sector_t iblock, off_t* result)
{
    if (INODE_VERSION(inode) == MINIX_V1)
        return minix_get_block_v1(inode, iblock, result);
    else
        return minix_get_block_v2(inode, iblock, result);
}

int
minixfs_get_page(struct inode* inode, sector_t index, char* pagebuf)
{
    sector_t iblock, lblock;
    unsigned int blocksize;
    int nr, i;

    blocksize = 1 << inode->I_blkbits;

    iblock = index << (PAGE_CACHE_SHIFT - inode->I_blkbits);
    lblock = (inode->I_size + blocksize - 1) >> inode->I_blkbits;

    nr = 0;
    i = 0;

    int bytes = 0, err = 0;
    struct super_block* sb = inode->I_sb;
    struct buffer_head _bh;
    char* p = pagebuf;

    do {
        off_t phys64 = 0;
        int ret = minixfs_get_block(inode, iblock, &phys64);
        if (phys64) {
            struct buffer_head* bh = &_bh;
            bh->b_flags.dynamic = 0;
            if (sb_bread_intobh(sb, phys64, bh) == 0) {
                memcpy(p, bh->b_data, blocksize);
                p += blocksize;
                bytes += blocksize;
                brelse(bh); 
            } else {
                err = EIO;
                fprintf(stderr, "*** fatal error: I/O error reading page\n");
                abort();
                exit(10);
            }
        } else if (ret == 0) { /* zero fill */
            memset(p, 0, blocksize);
            p += blocksize;
            bytes += blocksize;
        } else {
            err = EIO;
            fprintf(stderr, "*** fatal error: block mapping failed\n");
            abort();
        }

        iblock++;

        if ((bytes >= PAGE_SIZE) || (iblock >= lblock))
            break;

    } while (1);

    if (err)
        return -1;

    /* check page? */

    return 0;
}
