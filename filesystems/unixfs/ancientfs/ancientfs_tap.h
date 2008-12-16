/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_TAP_H_
#define _ANCIENTFS_TAP_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define BSIZE   512

#define ROOTINO  1
#define DIRSIZ   14
#define PATHSIZ  32

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    struct inode* s_rootip;
};

struct dinode_tap {
    uint8_t  di_path[PATHSIZ]; /* if di_path[0] == 0, the entry is empty */
    uint8_t  di_mode;  /* type and permissions */
    uint8_t  di_uid;   /* owner's id */
    uint16_t di_size;  /* file's size */
    uint32_t di_mtime; /* file's modification time */
    uint16_t di_addr;  /* tape block of the start of the file's contents */
    uint8_t  di_unused[20];
    uint16_t di_cksum; /* sum of the 32 words of the directory is 0 */
} __attribute__((packed));

#define INOPB 8

struct tap_node_info {
    struct inode* ti_self;
    uint8_t ti_name[DIRSIZ];
    struct tap_node_info* ti_parent;
    struct tap_node_info* ti_children;
    struct tap_node_info* ti_next_sibling;
};

/* flags */
#define ILOCK   01
#define IUPD    02
#define IACC    04
#define IMOUNT  010
#define IWANT   020
#define ITEXT   040

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define IFMT    060000
#define IFDIR   040000  /* directory */
#define IMOD    020000  /* file has been modified (always on) */
#define ILARG   010000  /* large file */
/* v1/v2/v3 modes */
#define ISUID   000040  /* set user ID on execution */
#define IEXEC   000020  /* executable */
#define IRUSR   000010  /* read, owner */
#define IWUSR   000004  /* write, owner */
#define IROTH   000002  /* read, non-owner */
#define IWOTH   000001  /* write, non-owner */

#include <sys/stat.h>

static inline mode_t
ancientfs_tap_mode(mode_t mode, uint32_t flags)
{
    mode_t newmode = 0;

    mode = mode & ~(IALLOC | ILARG);

    if ((flags & ANCIENTFS_UNIX_V1) ||
        (flags & ANCIENTFS_UNIX_V2) ||
        (flags & ANCIENTFS_UNIX_V3))
        goto tap;

    /* ntap */

    /* we synthesize directories ourselves, so mode is OK as it is */
    if (S_ISDIR(mode))
        return mode;

    /* translate ntap mode bits */

    if ((mode & IFMT) == IFDIR)
        newmode |= S_IFDIR;
    else
        newmode |= S_IFREG;

    if (mode & 040)
        newmode |= S_IRUSR;
    if (mode & 020)
        newmode |= S_IWUSR;
    if (mode & 010)
        newmode |= S_IXUSR;
    if (mode & 004)
        newmode |= S_IROTH;
    if (mode & 002)
        newmode |= S_IWOTH;
    if (mode & 001)
        newmode |= S_IXOTH;

    return newmode;

tap:

    /* we synthesize directories ourselves, so mode is OK as it is */
    if (S_ISDIR(mode))
        return mode;

    /* translate tap mode bits */

    if ((mode & IFMT) == IFDIR)
        newmode |= S_IFDIR;
    else
        newmode |= S_IFREG;

    if (mode & ISUID)
        newmode |= S_ISUID;

    if (mode & IEXEC)
        newmode |= S_IXUSR;

    if (mode & IRUSR)
        newmode |= S_IRUSR;

    if (mode & IWUSR)
        newmode |= S_IWUSR;

    if (mode & IROTH) {
        newmode |= S_IROTH;
        if (mode & IEXEC)
            newmode |= S_IXOTH;
    }

    if (mode & IWOTH)
        newmode |= S_IWOTH;

    return newmode;
}

static inline int
ancientfs_tap_cksum(uint8_t* de, fs_endian_t e, uint32_t flags)
{
    uint16_t* p = (uint16_t*)de;
    uint16_t cksum = 0;
    int i = 0;
    for (i = 0; i < 32; i++, p++)
        cksum += fs16_to_host(e, *p);
    return cksum;
}

/*
 * v1 00:00 Jan 1, 1971 /60
 * v2 00:00 Jan 1, 1971 /60
 * v3 00:00 Jan 1, 1972 /60
 *
 * modern 00:00 Jan 1, 1970 full seconds
 */
static inline uint32_t
ancientfs_tap_time(uint32_t t, uint32_t flags)
{
    if (!(flags & (ANCIENTFS_UNIX_V1 | ANCIENTFS_UNIX_V2 | ANCIENTFS_UNIX_V3)))
        return t;

    uint32_t cvt = t;

    cvt = cvt / 60; /* times were measured in sixtieths of a second */

    uint32_t epoch_years = (flags & ANCIENTFS_UNIX_V1) ? 1 :
                           (flags & ANCIENTFS_UNIX_V2) ? 1 :
                           (flags & ANCIENTFS_UNIX_V3) ? 2 : 0;

    cvt += (epoch_years * 365 * 24 * 3600); /* epoch fixup */

    return cvt;
}

#endif /* _ANCIENTFS_TAP_H_ */
