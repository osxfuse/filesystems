/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_211BSD_H_
#define _ANCIENTFS_211BSD_H_

#include "unixfs_internal.h"

typedef uint8_t  a_uchar;    /* ancient unsigned char */
typedef int16_t  a_short;    /* ancient short */
typedef uint16_t a_ushort;   /* ancient unsigned short */
typedef int16_t  a_int;      /* ancient int */
typedef uint16_t a_uint;     /* ancient unsigned int */
typedef int32_t  a_long;     /* ancient long */
typedef uint32_t a_ulong;    /* ancient unsigned long */

typedef a_ushort a_ino_t;    /* ancient inode type (ancient unsigned int) */
typedef a_long   a_off_t;    /* ancient offset type (ancient long) */
typedef a_long   a_time_t;   /* ancient time type (ancient long) */
typedef a_long   a_daddr_t;  /* ancient disk address type (ancient long) */
typedef a_short  a_dev_t;    /* ancient device type (ancient int) */
typedef a_ushort a_uid_t;    /* ancient user id type (ancient unsigned short) */
typedef a_ushort a_gid_t;    /* ancient grp id type (ancient unsigned short) */

/*
 * The file system is made out of blocks of MAXBSIZE units.
 */
#define MAXBSIZE   1024

/*
 * MAXPATHLEN defines the longest permissable path length
 * after expanding symbolic links. It is used to allocate
 * a temporary buffer from the buffer pool in which to do the
 * name expansion, hence should be a power of two, and must
 * be less than or equal to MAXBSIZE.
 * MAXSYMLINKS defines the maximum number of symbolic links
 * that may be expanded in a path name. It should be set high
 * enough to allow all legitimate uses, but halt infinite loops
 * reasonably quickly.
 */
#define MAXPATHLEN      256
#define MAXSYMLINKS     8

#undef  CLSIZE
#define CLSIZE     2           /* number of blocks / cluster */
#define DEV_BSIZE  1024        /* size of secondary block (bytes) */
#define NINDIR     (DEV_BSIZE/sizeof(a_daddr_t))
#define DEV_BMASK  0x3ffL      /* DEV_BSIZE - 1 */
#define DEV_DBMASK 0777L       /* directory block */
#define DEV_BSHIFT 10          /* LOG2(DEV_BSIZE) */
#define NMASK      0377L       /* NINDIR - 1 */
#define NSHIFT     8           /* LOG2(NINDIR) */

/*
 * The root inode is the root of the file system.
 * Inode 0 can't be used for normal purposes and
 * historically bad blocks were linked to inode 1,
 * thus the root inode is 2. (inode 1 is no longer used for
 * this purpose, however numerous dump tapes make this
 * assumption, so we are stuck with it)
 * The lost+found directory is given the next available
 * inode when it is created by ``mkfs''.
 */
#define ROOTINO  2   /* i number of all roots */

#define SUPERB   ((a_daddr_t)1) /* block number of the super block */
#define SBSIZE   DEV_BSIZE      /* super block size */
#define NICFREE  50             /* number of super block free blocks */
#define NICINOD  100            /* number of super block inodes */

#define MAXMNTLEN 12
#define MAXNAMLEN 63

/*
 * Definition of the unix super block.
 */
struct fs
{
    a_ushort  s_isize;            /* size in blocks of i-list */
    a_daddr_t s_fsize;            /* size in blocks of entire volume */
    a_short   s_nfree;            /* number of addresses in s_free */
    a_daddr_t s_free[NICFREE];    /* free block list */
    a_short   s_ninode;           /* number of i-nodes in s_inode */
    a_ino_t   s_inode[NICINOD];   /* free i-node list */
    char      s_flock;            /* lock during free list manipulation */
    char      s_ilock;            /* lock during i-list manipulation */
    char      s_fmod;             /* super block modified flag */
    char      s_ronly;            /* mounted read-only flag */
    time_t    s_time;             /* last super block update */
    a_daddr_t s_tfree;            /* total free blocks*/
    a_ino_t   s_tinode;           /* total free inodes */
    a_short   s_dinfo[2];         /* interleave stuff */
#define s_m   s_dinfo[0]          /* optimal step in free list pattern */
#define s_n   s_dinfo[1]          /* number of blocks per pattern */
    char      s_fsmnt[MAXMNTLEN]; /* ordinary file mounted on */
    ino_t     s_lasti;            /* start place for circular search */
    ino_t     s_nbehind;          /* est # free inodes before s_lasti */
    a_ushort  s_flags;            /* mount time flags */
/* actually longer */
} __attribute__((packed));

struct icommon2 {
    a_time_t ic_atime;
    a_time_t ic_mtime;
    a_time_t ic_ctime;
} __attribute__((packed));

struct icommon1 {
    a_ushort ic_mode;  /* mode and type of file */
    a_ushort ic_nlink; /* number of links to file */
    a_uid_t  ic_uid;   /* owner's user id */
    a_gid_t  ic_gid;   /* owner's group id */
    a_off_t  ic_size;  /* number of bytes in file */
} __attribute__((packed));

/*
 * Inode struct as it appears on a disk block.
 */
struct dinode
{
    struct    icommon1 di_icom1;
    a_daddr_t di_addr[7];     /* 7 block addresses 4 bytes each */
    a_ushort  di_reserved[5]; /* pad of 10 to make total size 64 */
    a_ushort  di_flags;
    struct    icommon2 di_icom2;
} __attribute__((packed));

#define di_ic1   di_icom1
#define di_ic2   di_icom2
#define di_mode  di_icom1.ic_mode
#define di_nlink di_icom1.ic_nlink
#define di_uid   di_icom1.ic_uid
#define di_gid   di_icom1.ic_gid
#define di_size  di_icom1.ic_size
#define di_atime di_icom2.ic_atime
#define di_mtime di_icom2.ic_mtime
#define di_ctime di_icom2.ic_ctime

/*
 * Turn file system block numbers into disk block addresses.
 * This maps file system blocks to device size blocks.
 */
#define fsbtodb(b) ((a_daddr_t)((a_daddr_t)(b) << 1))
#define dbtofsb(b) ((a_daddr_t)((a_daddr_t)(b) >> 1))

/*
 * Macros for handling inode numbers:
 *     inode number to file system block offset.
 *     inode number to file system block address.
 */
#define itoo(x) ((a_int)(((x) + 2 * INOPB - 1) % INOPB))
#define itod(x) ((a_daddr_t)((((a_uint)(x) + 2 * INOPB - 1) / INOPB)))

/*
 * The following macros optimize certain frequently calculated
 * quantities by using shifts and masks in place of divisions
 * modulos and multiplications.
 */
#define blkoff(loc) ((loc) & DEV_BMASK)   /* calculates (loc % fs->fs_bsize) */
#define lblkno(loc) ((loc) >> DEV_BSHIFT) /* calculates (loc / fs->fs_bsize) */

/*
 * INOPB is the number of inodes in a secondary storage block.
 */
#define INOPB 16 /* MAXBSIZE / sizeof(struct dinode) */

/*
 * 28 of the di_addr address bytes are used; 7 addresses of 4
 * bytes each: 4 direct (4Kb directly accessible) and 3 indirect.
 */
#define NDADDR 4                 /* direct addresses in inode */
#define NIADDR 3                 /* indirect addresses in inode */
#define NADDR  (NDADDR + NIADDR) /* total addresses in inode */

/* i_flag */
#define ILOCKED         0x1             /* inode is locked */
#define IUPD            0x2             /* file has been modified */
#define IACC            0x4             /* inode access time to be updated */
#define IMOUNT          0x8             /* inode is mounted on */
#define IWANT           0x10            /* some process waiting on lock */
#define ITEXT           0x20            /* inode is pure text prototype */
#define ICHG            0x40            /* inode has been changed */
#define ISHLOCK         0x80            /* file has shared lock */
#define IEXLOCK         0x100           /* file has exclusive lock */
#define ILWAIT          0x200           /* someone waiting on file lock */
#define IMOD            0x400           /* inode has been modified */
#define IRENAME         0x800           /* inode is being renamed */
#define IPIPE           0x1000          /* inode is a pipe */
#define IRCOLL          0x2000          /* read select collision on pipe */
#define IWCOLL          0x4000          /* write select collision on pipe */
#define IXMOD           0x8000          /* inode is text, but impure (XXX) */

/* i_mode */
#define IFMT            0170000         /* type of file */
#define IFCHR           0020000         /* character special */
#define IFDIR           0040000         /* directory */
#define IFBLK           0060000         /* block special */
#define IFREG           0100000         /* regular */
#define IFLNK           0120000         /* symbolic link */
#define IFSOCK          0140000         /* socket */
#define ISUID           04000           /* set user id on execution */
#define ISGID           02000           /* set group id on execution */
#define ISVTX           01000           /* save swapped text even after use */
#define IREAD           0400            /* read, write, execute permissions */
#define IWRITE          0200
#define IEXEC           0100

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

struct fblk {
    a_short   df_nfree;
    a_daddr_t df_free[NICFREE];
} __attribute__((packed));

#endif /* _ANCIENTFS_211BSD_H_ */
