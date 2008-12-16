/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_29BSD_H_
#define _ANCIENTFS_29BSD_H_

#include "unixfs_internal.h"

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

#define UCB_NKB 1

#if UCB_NKB == 1
#undef  CLSIZE
#define CLSIZE   2              /* number of blocks / cluster */
#define BSIZE    1024           /* size of secondary block (bytes) */
#define NINDIR   (BSIZE/sizeof(a_daddr_t))
#define BMASK    01777          /* BSIZE - 1 */
#define BSHIFT   10             /* LOG2(BSIZE) */
#define NMASK    0377           /* NINDIR - 1 */
#define NSHIFT   8              /* LOG2(NINDIR) */
#else
#define BSIZE    512            /* size of secondary block (bytes) */
#define NINDIR   (BSIZE/sizeof(a_daddr_t))
#define BMASK    0777           /* BSIZE - 1 */
#define BSHIFT   9              /* LOG2(BSIZE) */
#define NMASK    0177           /* NINDIR - 1 */
#define NSHIFT   7              /* LOG2(NINDIR) */
#endif

#define ROOTINO  2              /* i number of all roots */
#define DIRSIZ   14             /* max characters per directory */
#define NICINOD  100            /* number of super block inodes */
#define NICFREE  50             /* number of super block free blocks */

/*
 * Definition of the unix super block.
 */
struct filsys
{
    a_ushort  s_isize;          /* size in blocks of i-list */
    a_daddr_t s_fsize;          /* size in blocks of entire volume */
    a_short   s_nfree;          /* number of addresses in s_free */
    a_daddr_t s_free[NICFREE];  /* free block list */
    a_short   s_ninode;         /* number of i-nodes in s_inode */
    a_ino_t   s_inode[NICINOD]; /* free i-node list */
    char      s_flock;          /* lock during free list manipulation */
    char      s_ilock;          /* lock during i-list manipulation */
    char      s_fmod;           /* super block modified flag */
    char      s_ronly;          /* mounted read-only flag */
    time_t    s_time;           /* last super block update */

    /* remainder not maintained by this version of the system */

    a_daddr_t s_tfree;          /* total free blocks*/
    a_ino_t   s_tinode;         /* total free inodes */
    a_short   s_dinfo[2];       /* interleave stuff */
#define s_m   s_dinfo[0]
#define s_n   s_dinfo[1]
    char      s_fsmnt[12];      /* ordinary file mounted on */
    ino_t     s_lasti;          /* start place for circular search */
    ino_t     s_nbehind;        /* est # free inodes before s_lasti */
} __attribute__((packed));

/*
 * Inode struct as it appears on a disk block.
 */
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

#if UCB_NKB == 1
#define INOPB 16 /* 16 inodes per block */
/*
 * 21 of the address bytes are used;
 * 7 addresses of 3 bytes each.
 */
#else
#define INOPB 8 /* 8 inodes per block */
/*
 * 39 of the address bytes are used;
 * 13 addresses of 3 bytes each.
 */
#endif

/* inumber to disk address */
#if UCB_NKB == 1
#define itod(x) ((a_daddr_t)((((a_uint)(x) + 2 * INOPB - 1) / INOPB)))
#else
#define itod(x) (a_daddr_t)((((a_uint)(x) + 15) >> 3))
#endif

/* inumber to disk offset */
#if UCB_NKB == 1
#define itoo(x) ((a_int)(((x) + 2 * INOPB - 1) % INOPB))
#else
#define itoo(x) (a_int)(((x) + 15) & 07)
#endif

#if UCB_NKB == 1
#define NADDR  7
#define NINDEX 6
#else
#define NADDR 13
#define NINDEX 15
#endif

/* flags */
#define ILOCK   01      /* inode is locked */
#define IUPD    02      /* inode has been modified */
#define IACC    04      /* inode access time to be updated */
#define IMOUNT  010     /* inode is mounted on */
#define IWANT   020     /* some process waiting on lock */
#define ITEXT   040     /* inode is pure text prototype */
#define ICHG    0100    /* inode has been changed */
#define IPIPE   0200    /* inode is a pipe */
#ifdef  UCB_QUOTAS
#define IQUOT   0400    /* directory that has original quota pointer */
#endif

/* modes */
#define IFMT    0170000 /* type of file */
#define IFDIR   0040000 /* directory */
#define IFCHR   0020000 /* character special */
#define IFMPC   0030000 /* multiplexed char special */
#define IFBLK   0060000 /* block special, 0 is regular */
#define IFMPB   0070000 /* multiplexed block special */
#define IFREG   0100000 /* regular */
#define IFLNK   0120000 /* symbolic link */
#define IFQUOT  0140000 /* quota */
#define ISUID   04000   /* set user id on execution */
#define ISGID   02000   /* set group id on execution */
#define ISVTX   01000   /* save swapped text even after use */
#define IREAD   0400    /* read, write, execute permissions */
#define IRWRITE 0200
#define IEXEC   0100

struct dent {
    a_ino_t u_ino;          /* inode table pointer */
    char    u_name[DIRSIZ]; /* component name */
} __attribute__((packed));

struct fblk {
    a_short   df_nfree;
    a_daddr_t df_free[NICFREE];
} __attribute__((packed));

#endif /* _ANCIENTFS_29BSD_H_ */
