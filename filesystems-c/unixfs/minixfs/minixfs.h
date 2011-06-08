/*
 * Minix File System Famiy for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of sysvfs. See fs/minix/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */

#ifndef _MINIXFS_H_
#define _MINIXFS_H_

#include "unixfs_internal.h"
#include <linux.h>
#include <linux/minix_fs.h>
#include <libkern/OSByteOrder.h>

#define INODE_VERSION(inode) minix_sb(inode->I_sb)->s_version

#define MINIX_V1 0x0001 /* original minix fs */
#define MINIX_V2 0x0002 /* minix V2 fs */
#define MINIX_V3 0x0003 /* minix V3 fs */

struct minix_inode_info {
    union {
        __u16 i1_data[16];
        __u32 i2_data[16];
    } u;
    u32 i_dir_start_lookup;
};

struct minix_sb_info {
    unsigned long s_ninodes;
    unsigned long s_nzones;
    unsigned long s_imap_blocks;
    unsigned long s_zmap_blocks;
    unsigned long s_firstdatazone;
    unsigned long s_log_zone_size;
    unsigned long s_max_size;
    int s_dirsize;
    int s_namelen;
    int s_link_max;
    struct buffer_head** s_imap;
    struct buffer_head** s_zmap;
    struct buffer_head*  s_sbh;
    struct minix_super_block* s_ms;
    unsigned short s_mount_state;
    unsigned short s_version;
};

typedef struct minix_dir_entry minix_dirent;
typedef struct minix3_dir_entry minix3_dirent;

static inline struct minix_sb_info* minix_sb(struct super_block* sb)
{
    return (struct minix_sb_info*)sb->s_fs_info;
}

static inline struct minix_inode_info* minix_i(struct inode* inode)
{
    return (struct minix_inode_info*)inode->I_private;
}

static inline unsigned long minix_dir_pages(struct inode* inode)
{
    return (inode->I_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

static inline int minix_namecompare(int len, int maxlen, const char* name,
                                    const char* buffer)
{
    if (len < maxlen && buffer[len])
        return 0;
    return !memcmp(name, buffer, len);
}

#if defined(__LITTLE_ENDIAN__)
#define minix_set_bit(nr,addr)          \
        generic___set_le_bit((nr),(unsigned long *)(addr))
#elif defined(__BIG_ENDIAN__)
#define minix_set_bit(nr,addr)          \
        __set_bit((nr),(unsigned long *)(addr))
#else
#error Endian problem
#endif

struct super_block*
      minixfs_fill_super(int fd, void* args, int silent);
int   minixfs_statvfs(struct super_block* sb, struct statvfs* buf);
int   minixfs_iget(struct super_block* sb, struct inode* ip);
ino_t minixfs_inode_by_name(struct inode* dir, const char* name);
int   minixfs_next_direntry(struct inode* dir, struct unixfs_dirbuf* dirbuf,
                          off_t* offset, struct unixfs_direntry* dent);
int   minixfs_get_block(struct inode* ip, sector_t fragment, off_t* result);
int   minixfs_get_page(struct inode* ip, sector_t index, char* pagebuf);

#endif /* _MINIXFS_H_ */
