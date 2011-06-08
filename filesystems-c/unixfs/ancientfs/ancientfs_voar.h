/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_VOAR_H_
#define _ANCIENTFS_VOAR_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t  a_short;       /* ancient short */
typedef uint16_t a_ushort;      /* ancient unsigned short */
typedef int16_t  a_int;         /* ancient int */
typedef uint16_t a_uint;        /* ancient unsigned int */
typedef int32_t  a_long;        /* ancient long */
typedef uint32_t a_ulong;       /* ancient unsigned long */

typedef a_uint   a_ino_t;       /* ancient inode type (ancient unsigned int) */
typedef a_long   a_off_t;       /* ancient offset type (ancient long) */
typedef a_long   a_time_t;      /* ancient time type (ancient long) */
typedef a_long   a_daddr_t;     /* ancient disk address type (ancient long) */
typedef a_int    a_dev_t;       /* ancient device type (ancient int) */

#define BSIZE    512
#define BMASK    0777           /* BSIZE - 1 */

#define ROOTINO  1
#define DIRSIZ   8

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    struct inode* s_rootip;
};

#define ARMAG  (uint16_t)0177555

struct ar_hdr
{
    char   ar_name[DIRSIZ];
    a_long ar_date;
    char   ar_uid;
    char   ar_mode;
    a_int  ar_size;
} __attribute__((packed));

struct ar_node_info
{ 
    struct inode* ar_self;
    uint8_t ar_name[DIRSIZ + 1];
    struct ar_node_info* ar_parent;
    struct ar_node_info* ar_children;
    struct ar_node_info* ar_next_sibling;
};

/* modes */
#define IALLOC  0100000 /* i-node is allocated */
#define IFMT    060000
#define IFDIR   040000  /* directory */
#define IMOD    020000  /* file has been modified (always on) */
#define ILARG   010000  /* large file */

/* v1 mode bits */
#define ISUID   000040  /* set user ID on execution */
#define IEXEC   000020  /* executable */
#define IRUSR   000010  /* read, owner */
#define IWUSR   000004  /* write, owner */
#define IROTH   000002  /* read, non-owner */
#define IWOTH   000001  /* write, non-owner */

#include <sys/stat.h>

static inline a_int
ancientfs_ar_mode(mode_t mode, uint32_t flags)
{
    mode_t newmode = 0;

    if ((flags & ANCIENTFS_UNIX_V1) ||
        (flags & ANCIENTFS_UNIX_V2) ||
        (flags & ANCIENTFS_UNIX_V3))
        goto old;

    newmode = (uint8_t)mode & ~(IALLOC | ILARG);
    if ((newmode & IFMT) == 0) /* ancient regular file */
        newmode |= S_IFREG;
    return newmode;

old:

    /* older mode bits */

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

/*
 * v1 00:00 Jan 1, 1971 /60
 * v2 00:00 Jan 1, 1971 /60
 * v3 00:00 Jan 1, 1972 /60
 *
 * modern 00:00 Jan 1, 1970 full seconds
 */
static inline uint32_t
ancientfs_ar_time(uint32_t t, uint32_t flags)
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

#endif /* _ANCIENTFS_VOAR_H_ */
