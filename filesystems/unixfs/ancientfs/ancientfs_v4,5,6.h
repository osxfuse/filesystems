/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_V456_H_
#define _ANCIENTFS_V456_H_

/*
 * V4 += DIRSIZ 14, size0/size1, groups, 1970 epoch with full seconds
 * V5 += ISVTX
 */

#include "unixfs_internal.h"
#include "ancientfs.h"

typedef int16_t a_ino_t; /* ancient inode type */
typedef int16_t a_int;   /* ancient int */

#define BSIZE   512

#define ROOTINO 1         /* i number of all roots */
#define SUPERB  1         /* block number of the super block */
#define DIRSIZ  14        /* max characters per directory */

/*
 * Definition of the unix super block.
 */
struct filsys
{
    a_int   s_isize;      /* size in blocks of the I list */
    a_int   s_fsize;      /* size in blocks of entire volume */
    a_int   s_nfree;      /* number of in core free blocks (0 - 100) */
    a_int   s_free[100];  /* in core free blocks */
    a_int   s_ninode;     /* number of in core I nodes (0 - 100) */
    a_ino_t s_inode[100]; /* in core free I nodes */
    char    s_flock;      /* lock during free list manipulation */
    char    s_ilock;      /* lock during I list manipulation */
    char    s_fmod;       /* super block modified flag */
    char    s_ronly;      /* mounted read-only flag */
    a_int   s_time[2];    /* current date of last update */
    a_int   pad[50];
} __attribute__((packed));

/*
 * Inode struct as it appears on the disk.
 */
struct dinode
{
    a_int di_mode;     /* type and permissions */
    char  di_nlink;    /* directory entries */
    char  di_uid;      /* owner */
    char  di_gid;      /* group of owner */
    char  di_size0;    /* most significant of size */
    a_int di_size1;    /* least significant of size */
    a_int di_addr[8];  /* device addresses constituting file */
    a_int di_atime[2]; /* access time */
    a_int di_mtime[2]; /* modification time */
} __attribute__((packed));

/* flags */
#define ILOCK   01      /* inode is locked */
#define IUPD    02      /* inode has been modified */
#define IACC    04      /* inode access time to be updated */
#define IMOUNT  010     /* inode is mounted on */
#define IWANT   020     /* some process waiting on lock */
#define ITEXT   040     /* inode is pure text prototype */

/* modes */
#define IALLOC  0100000 /* file is used */
#define IFMT    060000  /* type of file */
#define IFDIR   040000  /* directory */
#define IFCHR   020000  /* character special */
#define IFBLK   060000  /* block special, 0 is regular */
#define ILARG   010000  /* large addressing algorithm */
#define ISUID   04000   /* set user id on execution */
#define ISGID   02000   /* set group id on execution */
#define ISVTX   01000   /* save swapped text even after use */
#define IREAD   0400    /* read, write, execute permissions */
#define IRWRITE 0200
#define IEXEC   0100

static inline a_int
ancientfs_v456_mode(mode_t mode)
{
    a_int newmode = (a_int)mode & ~(IALLOC | ILARG);
    if ((newmode & IFMT) == 0) /* ancient regular file */
        newmode |= S_IFREG;
    return newmode;
}

struct dent {
    a_ino_t u_ino;          /* inode table pointer */
    char    u_name[DIRSIZ]; /* component name */
} __attribute__((packed));

#endif /* _ANCIENTFS_V456_H_ */
