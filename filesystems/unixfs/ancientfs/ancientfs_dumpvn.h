/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_DUMPVN_H_
#define _ANCIENTFS_DUMPVN_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t  a_short;      /* ancient short */
typedef uint16_t a_ushort;     /* ancient unsigned short */
typedef int16_t  a_int;        /* ancient int */
typedef uint16_t a_uint;       /* ancient unsigned int */
typedef int32_t  a_long;       /* ancient long */
typedef uint32_t a_ulong;      /* ancient unsigned long */

typedef a_uint   a_ino_t;      /* ancient inode type (ancient unsigned int) */
typedef a_long   a_off_t;      /* ancient offset type (ancient long) */
typedef a_ulong  a_time_t;     /* ancient time type (ancient long) */
typedef a_long   a_daddr_t;    /* ancient disk address type (ancient long) */
typedef a_int    a_dev_t;      /* ancient device type (ancient int) */

#if UCB_NKB == 1
#undef  CLSIZE
#define CLSIZE 2               /* number of blocks / cluster */
#define BSIZE  1024            /* size of secondary block (bytes) */
#define NINDIR (BSIZE/sizeof(a_daddr_t))
#define BMASK  01777           /* BSIZE - 1 */
#define BSHIFT 10              /* LOG2(BSIZE) */
#define NMASK  0377            /* NINDIR - 1 */
#define NSHIFT 8               /* LOG2(NINDIR) */
#else
#define BSIZE  512             /* logical record size */
#define BMASK  0777            /* BSIZE - 1 */
#define BSHIFT 9               /* LOG2(BSIZE) */
#define NINDIR (BSIZE/sizeof(a_daddr_t))
#define NMASK  0177            /* NINDIR - 1 */
#define NSHIFT 7               /* LOG2(NINDIR) */
#endif

#define BADINO   1
#define ROOTINO  2
#define DIRSIZ   14

/* <dumprestor.h> */

#define TAPE_BSIZE 10240       /* physical tape block size */

#define NTREC TAPE_BSIZE/BSIZE /* number of logical records in a tape block */
#define MLEN  16               /* number of bits in a bit map word */
#define MSIZ  4096             /* number of words in a bit map */

#define TS_TAPE  1             /* tape volume label */
#define TS_INODE 2             /* a file or directory follows (c_dinode) */
#define TS_BITS  3             /* bitmap follows; 1 bit for each inode dumped */
#define TS_ADDR  4             /* a subrecord of a file description (c_addr) */
#define TS_END   5             /* end of tape record */
#define TS_CLRI  6             /* bitmap follows; 0 bit for empty inodes */
#define MAGIC    (a_uint)60011 /* all header records have this in c_magic */
#define CHECKSUM (a_uint)84446 /* all header records checksum to this value */

struct filsys
{
    uint32_t      s_fsize;
    uint32_t      s_files;
    uint32_t      s_directories;
    a_ino_t       s_dumpmap[MSIZ];
    a_time_t      s_date;
    a_time_t      s_ddate;
    uint32_t      s_initialized;
    ino_t         s_lastino;
    struct inode* s_rootip;
};

struct dinode
{
    a_ushort di_mode;     /* mode and type of file */
    a_short  di_nlink;    /* number of links to file */
    a_short  di_uid;      /* owner's user id */
    a_short  di_gid;      /* owner's group id */
    a_off_t  di_size;     /* number of bytes in file */
    char     di_addr[40]; /* disk block addresses */
    a_time_t di_atime;    /* time last accessed */
    a_time_t di_mtime;    /* time last modified */
    a_time_t di_ctime;    /* time created */
} __attribute__((packed));

struct tap_node_info {
    uint32_t* ti_daddr; 
};

#define ANCIENTFS_211BSD_DIRBLKSIZ 512

struct direct {
    a_ino_t  d_ino;    /* inode number of entry */
    a_ushort d_reclen; /* length of this record */
    a_ushort d_namlen; /* length of string in d_name */
    char     d_name[UNIXFS_MAXNAMLEN + 1];
} __attribute__((packed));

#define ANCIENTFS_211BSD_DIRSIZ(dp) \
    ((((sizeof(struct direct) - (UNIXFS_MAXNAMLEN + 1)) + \
        (dp)->d_namlen + 1) + 3) & ~3)

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define blkoff(loc) ((loc) & BMASK)   /* calculates (loc % fs->fs_bsize) */
#define lblkno(loc) ((loc) >> BSHIFT) /* calculates (loc / fs->fs_bsize) */

struct spcl {
    a_int         c_type;      /* type of the header */
    a_time_t      c_date;      /* date the dump was taken */
    a_time_t      c_ddate;     /* date the file system was dumped from */
    a_int         c_volume;    /* current volume number of the dump */
    a_daddr_t     c_tapea;     /* current number of this (512-byte) record */
    a_ino_t       c_inumber;   /* # of the inode being dumped (TS_INODE) */
    a_uint        c_magic;     /* contains the value MAGIC */
    a_int         c_checksum;  /* contains whatever value needed to checksum */
    struct dinode c_dinode;    /* copy of on-disk inode */
    a_int         c_count;     /* count of characters in c_addr */
    char          c_addr[BSIZE - 88];
} __attribute__((packed));

#define MWORD16(m, n)  m[(uint16_t)(n - 1)/MLEN]
#define MBIT16(n)      (1 << ((uint16_t)(n - 1) % MLEN))
#define BIT_ON(n, w)   (MWORD16(w, n) & MBIT16(n))

static inline int
ancientfs_dump_cksum(uint16_t* buf, fs_endian_t e, uint32_t flags)
{
    uint16_t sum;
    uint16_t limit;

    sum = 0;
    limit = BSIZE / sizeof(uint16_t);

    do {
        sum += fs16_to_host(e, *buf);
        buf++;
    } while (--limit);

    return (sum == CHECKSUM) ? 0 : 1;
}

#endif /* _ANCIENTFS_DUMPVN_H_ */
