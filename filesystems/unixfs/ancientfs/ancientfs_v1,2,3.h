/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_V123_H_
#define _ANCIENTFS_V123_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t a_int;   /* ancient int */
typedef a_int   a_ino_t; /* ancient i-node type */

#define BSIZE   512

#define ROOTINO 41
#define DIRSIZ  8

/*
 * Disk blocks 0 and 1 are collectively known as the super-block
 */

#define SUPERB  (off_t)0
#define SBSIZE  BSIZE * 2

struct filsys
{
    a_int    s_bmapsz;     /* free storage map bytes; always even */
    uint8_t* s_bmap;       /* free storage map */
    a_int    s_imapsz;     /* i-node map bytes; always even */
    uint8_t* s_imap;       /* i-node map */
} __attribute__((packed));

/*
 * b-map
 *
 * There is one bit for each block on the device; the bit is "1" if the
 * block is free. Thus, if s_bmapsz is n, the blocks on the device are
 * numbered 0 through 8n - 1. In s_bmap, the bit for block k of the device
 * is in byte k/8 of the map; it is offset k (mod 8) bits from the right.
 * Note that bits exist for the super-block and the i-node list, even though
 * they are never allocated or freed.
 */

 /*
  * i-map
  *
  * s_imapsz is also even. I-numbers below 41 are reserved for special files,
  * and are never allocated. The first bit in s_imap refers to i-number 41.
  * Therefore, the byte number in s_imap for i-node i is (i - 41)/8. It is
  * offset (i - 41) (mod 8) bits from the right; unlike s_bmap, a "0" bit
  * indicates an available i-node.
  */

struct dinode
{
    a_int   di_flags;      /* flags (see below) */
    char    di_nlink;      /* number of links */
    char    di_uid;        /* user ID of owner */
    a_int   di_size;       /* size in bytes */
    a_int   di_addr[8];    /* indirect block or contents block 1 through 8 */
    a_int   di_crtime[2];  /* creation time */
    a_int   di_mtime[2];   /* modification time */
    char    di_unused[2];  /* unused */
} __attribute__((packed)); /* 32 bytes */

/*
 * I-numbers begin at 1, and the storage for i-nodes begins at disk block 2.
 * Also, i-nodes are 32 bytes long, so 16 of them fit into a block. Therefore,
 * i-node i is located in block (i + 31)/16 of the file system, and begins
 * 32 * ((i + 31)(mod 16)) bytes from its start.
 */

#define INOPB 16           /* i-nodes per block */

/* flags */
#define IALLOC  0100000    /* i-node is allocated */
#define IFMT    060000
#define IFDIR   040000     /* directory */
#define IMOD    020000     /* file has been modified (always on) */
#define ILARG   010000     /* large file */
#define ISUID   000040     /* set user ID on execution */
#define IEXEC   000020     /* executable */
#define IRUSR   000010     /* read, owner */
#define IWUSR   000004     /* write, owner */
#define IROTH   000002     /* read, non-owner */
#define IWOTH   000001     /* write, non-owner */

static inline int
ancientfs_v123_mode(mode_t mode, uint32_t flags)
{
    int newmode = 0;

    mode = mode & ~(IALLOC | ILARG);

    if ((mode & IFMT) == 0)
        newmode |= S_IFREG;
    else
        newmode |= (mode & IFMT);
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
ancientfs_v123_time(uint32_t t, uint32_t flags)
{
    uint32_t cvt = t;

    cvt = cvt / 60; /* times were measured in sixtieths of a second */

    uint32_t epoch_years = (flags & ANCIENTFS_UNIX_V1) ? 1 :
                           (flags & ANCIENTFS_UNIX_V2) ? 1 :
                           (flags & ANCIENTFS_UNIX_V3) ? 2 : 0;

    cvt += (epoch_years * 365 * 24 * 3600); /* epoch fixup */

    return cvt;
}

struct dent {
    a_int u_ino;          /* i-node table pointer */
    char  u_name[DIRSIZ]; /* component name */
} __attribute__((packed));

#endif /* _ANCIENTFS_V123_H_ */
