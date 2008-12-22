/*
 * UnixFS
 *
 * A general-purpose file system layer for writing/reimplementing/porting
 * Unix file systems through MacFUSE.

 * Copyright (c) 2008 Amit Singh. All Rights Reserved.
 * http://osxbook.com
 */

#ifndef _UNIXFS_INTERNAL_H_
#define _UNIXFS_INTERNAL_H_

#include "unixfs.h"

#include <libgen.h>
#if __linux__
#include <darwin/queue.h>
#else
#include <sys/queue.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#if __APPLE__

#define ino64_t ino_t
#include <libkern/OSByteOrder.h>

#elif __linux__

#define ino64_t ino_t
extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);

#include <endian.h>
#include <asm/byteorder.h>

#define OSSwapLittleToHostInt64(x) __le64_to_cpu(x)
#define OSSwapBigToHostInt64(x)    __be64_to_cpu(x)
#define OSSwapHostToLittleInt64(x) __cpu_to_le64(x)
#define OSSwapHostToBigInt64(x)    __cpu_to_be64(x)
  
#define OSSwapLittleToHostInt32(x) __le32_to_cpu(x)
#define OSSwapBigToHostInt32(x)    __be32_to_cpu(x)
#define OSSwapHostToLittleInt32(x) __cpu_to_le32(x)
#define OSSwapHostToBigInt32(x)    __cpu_to_be32(x)
  
#define OSSwapLittleToHostInt16(x) __le16_to_cpu(x)
#define OSSwapBigToHostInt16(x)    __be16_to_cpu(x)
#define OSSwapHostToLittleInt16(x) __cpu_to_le16(x)
#define OSSwapHostToBigInt16(x)    __cpu_to_be16(x)

#if __BYTE_ORDER == __BIG_ENDIAN
#define __BIG_ENDIAN__ 1
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define __LITTLE_ENDIAN__ 1
#else
#error Endian Problem
#endif

#elif __FreeBSD__

#define ino64_t uint64_t
#include <sys/endian.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define __LITTLE_ENDIAN__ 1
#elif BYTE_ORDER == BIG_ENDIAN
#define __BIG_ENDIAN__ 1
#else
#error Endian Problem
#endif

#define OSSwapLittleToHostInt64(x) le64toh(x)
#define OSSwapBigToHostInt64(x)    htole64(x)
#define OSSwapLittleToHostInt32(x) le32toh(x)
#define OSSwapBigToHostInt32(x)    htole32(x)
#define OSSwapLittleToHostInt16(x) le16toh(x)
#define OSSwapBigToHostInt16(x)    htole16(x)

#endif

#define UNIXFS_ENABLE_INODEHASH   1 /* 1 => enable; 0 => disable */

#define UNIXFS_BADBLOCK(blk, err) ((err != 0))
#define UNIXFS_IOSIZE(unixfs)     (uint32_t)(unixfs->s_statvfs.f_bsize)
#define UNIXFS_NADDR_MAX          13

struct super_block {
    u_long         s_magic;
    u_long         s_flags;
    u_long         s_blocksize;
    uint8_t        s_blocksize_bits;
    uint8_t        s_dirt;
    fs_endian_t    s_endian;
    void*          s_fs_info;
    int            s_bdev; /* block device fd */
    int            s_kdev; /* kernel IPC descriptor */
    uint32_t       s_dentsize;
    char           s_fsname[UNIXFS_MNAMELEN];
    char           s_volname[UNIXFS_MAXNAMLEN];
    struct statvfs s_statvfs;
};

#define s_id s_fsname

#define MACFUSE_ROOTINO 1

/*
 * The I node is the focus of all
 * file activity in unix. There is a unique
 * inode allocated for each active file,
 * each current directory, each mounted-on
 * file, text file, and the root. An inode is 'named'
 * by its dev/inumber pair. (iget/iget.c)
 * Data, from mode on, is read in
 * from permanent inode on volume.
 *
 * This is a modern version of the in-core inode.
 * We use this for all ancient file systems we support.
 * We don't need a 'dev' though.
 */
typedef struct inode {
    LIST_ENTRY(inode)   I_hashlink;
    pthread_cond_t      I_state_cond;
    uint32_t            I_initialized;
    uint32_t            I_attachoutstanding;
    uint32_t            I_waiting;
    uint32_t            I_count;
    uint32_t            I_blkbits;
    struct super_block* I_sb;
    struct stat         I_stat;
    union {
        uint32_t        I_daddr[UNIXFS_NADDR_MAX];
        uint8_t         I_addr[UNIXFS_NADDR_MAX];
    } I_addr_un;
    void*               I_private;
} inode;

#define I_mode       I_stat.st_mode
#define I_nlink      I_stat.st_nlink
#define I_number     I_stat.st_ino
#define I_ino        I_stat.st_ino
#define I_uid        I_stat.st_uid
#define I_gid        I_stat.st_gid
#define I_rdev       I_stat.st_rdev
#define I_atime      I_stat.st_atimespec
#define I_mtime      I_stat.st_mtimespec
#define I_ctime      I_stat.st_ctimespec
#define I_crtime     I_stat.st_birthtimespec
#if __linux__
#define I_atime_sec  I_stat.st_atime
#define I_mtime_sec  I_stat.st_mtime
#define I_ctime_sec  I_stat.st_ctime
#else
#define I_atime_sec  I_stat.st_atimespec.tv_sec
#define I_mtime_sec  I_stat.st_mtimespec.tv_sec
#define I_ctime_sec  I_stat.st_ctimespec.tv_sec
#define I_crtime_sec I_stat.st_birthtimespec.tv_sec
#endif
#define I_size       I_stat.st_size
#define I_blocks     I_stat.st_blocks
#define I_blksize    I_stat.st_blksize
#define I_flags      I_stat.st_flags
#define I_gen        I_stat.st_gen
#define I_generation I_stat.st_gen
#define I_version    I_stat.st_gen
#define I_addr       I_addr_un.I_addr
#define I_daddr      I_addr_un.I_daddr

#define i_ino        I_stat.st_ino /* special case */

/* Inode layer interface. */

typedef int (*unixfs_inodelayer_iterator_t)(struct inode*, void*);

int           unixfs_inodelayer_init(size_t privsize);
void          unixfs_inodelayer_fini(void);
struct inode* unixfs_inodelayer_iget(ino_t ino);
void          unixfs_inodelayer_iput(struct inode* ip);
void          unixfs_inodelayer_isucceeded(struct inode* ip);
void          unixfs_inodelayer_ifailed(struct inode* ip);
void          unixfs_inodelayer_dump(unixfs_inodelayer_iterator_t);

/* Byte Swappers */

#define cpu_to_le32(x) OSSwapHostToLittleInt32(x)
#define cpu_to_be32(x) OSSwapHostToBigInt32(x)

static inline uint32_t
pdp11_to_host(uint32_t x)
{
#ifdef __LITTLE_ENDIAN__
    return ((x & 0xffff) << 16) | ((x & 0xffff0000) >> 16);
#else
#ifdef __BIG_ENDIAN__
    return ((x & 0xff00ff) << 8) | ((x & 0xff00ff00) >> 8);
#else
#error Endian Problem
#endif
#endif
}

static inline uint64_t
fs64_to_host(fs_endian_t e, uint64_t x)
{
    if (e == UNIXFS_FS_BIG)
        return OSSwapLittleToHostInt64(x);
    else
        return OSSwapBigToHostInt64(x);
}

static inline uint32_t
fs32_to_host(fs_endian_t e, uint32_t x)
{
    if (e == UNIXFS_FS_PDP)
        return pdp11_to_host(x);
    else if (e == UNIXFS_FS_LITTLE)
        return OSSwapLittleToHostInt32(x);
    else
        return OSSwapBigToHostInt32(x);
}

static inline uint16_t
fs16_to_host(fs_endian_t e, uint16_t x)
{
    if (e != UNIXFS_FS_BIG)
        return OSSwapLittleToHostInt16(x);
    else
        return OSSwapBigToHostInt16(x);
}

#endif /* _UNIXFS_INTERNAL_H_ */
