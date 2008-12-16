/*
 * SYSV File System Famiy for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of sysvfs. See fs/sysvfs/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */

#ifndef _SYSVFS_H_
#define _SYSVFS_H_

#include "unixfs_internal.h"
#include "linux.h"

#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Heuristic timestamp used to distinguish between SVR4 and SVR2 */
enum {
    JAN_1_1980 = (10*365 + 2) * 24 * 60 * 60
};

typedef __fs16 sysv_ino_t;  /* inode numbers are 16 bit */
typedef __fs32 sysv_zone_t; /* block numbers are 24 bit */

#define SYSV_INVAL_INO 0    /* inode 0 does not exist */
#define SYSV_BADBL_INO 1    /* inode of bad blocks file */
#define SYSV_ROOT_INO  2    /* inode of root directory */

struct sysv_dinode { /* on disk */
    __fs16 di_mode;
    __fs16 di_nlink;
    __fs16 di_uid;
    __fs16 di_gid;
    __fs32 di_size;
    u8     di_data[3 * (10 + 1 + 1 + 1)];
    u8     di_gen;
    __fs32 di_atime;
    __fs32 di_mtime;
    __fs32 di_ctime;
};

struct sysv_inode_info { /* in memory */
    __fs32  i_data[13];
    u32     i_dir_start_lookup;
};

static inline struct sysv_inode_info*
SYSV_I(struct inode* inode)
{
    return (struct sysv_inode_info*)&(inode[1]);
}

#define SYSV_NAMELEN 14

struct sysv_dir_entry { /* on disk */
    sysv_ino_t inode;
    char name[SYSV_NAMELEN];
};

#define SYSV_DIRSIZE sizeof(struct sysv_dir_entry)

static inline int
sysv_namecompare(int len, int maxlen, const char* name, const char* buffer)
{
    if (len < maxlen && buffer[len])
        return 0;
    return !memcmp(name, buffer, len);
}

static inline unsigned long
sysv_dir_pages(struct inode* inode)
{
        return (inode->I_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

struct sysv_sb_info { /* in memory */

    struct super_block* s_sb; /* VFS superblock */

    int  s_type;     /* file system type: FSTYPE_{XENIX|SYSV|COH} */
    char s_bytesex;  /* bytesex (le/be/pdp) */
    char s_truncate; /* if 1: names > SYSV_NAMELEN chars are truncated */
                     /* if 0: they are disallowed (ENAMETOOLONG) */

    nlink_t      s_link_max;              /* max # of hard links to a file */
    unsigned int s_inodes_per_block;      /* # of inodes per block */
    unsigned int s_inodes_per_block_1;    /* inodes_per_block - 1 */
    unsigned int s_inodes_per_block_bits; /* log2(inodes_per_block) */
    unsigned int s_ind_per_block;         /* # of indirections per block */
    unsigned int s_ind_per_block_bits;    /* log2(ind_per_block) */
    unsigned int s_ind_per_block_2;       /* ind_per_block ^ 2 */
    unsigned int s_toobig_block;          /* 10 + ipb + ipb^2 + ipb^3 */
    unsigned int s_block_base;            /* physical block # of block 0 */
    unsigned short s_fic_size;            /* free inode cache size, NICINOD */
    unsigned short s_flc_size;       /* free block list chunk size, NICFREE */

    struct buffer_head* s_bh1;       /* disk buffer 1 for holding superblock */
    struct buffer_head* s_bh2;       /* disk buffer 2 for holding superblock */

    char*        s_sbd1;                  /* entire superblock data, part 1 */
    char*        s_sbd2;                  /* entire superblock data, part 2 */
    __fs16*      s_sb_fic_count;          /* pointer to s_sbd->s_ninode */
    sysv_ino_t*  s_sb_fic_inodes;         /* pointer to s_sbd->s_inode */
    __fs16*      s_sb_total_free_inodes;  /* pointer to s_sbd->s_tinode */
    __fs16*      s_bcache_count;          /* pointer to s_sbd->s_nfree */
    sysv_zone_t* s_bcache;                /* pointer to s_sbd->s_free */
    __fs32*      s_free_blocks;           /* pointer to s_sbd->s_tfree */
    __fs32*      s_sb_time;               /* pointer to s_sbd->s_time */
    __fs32*      s_sb_state; /* pointer to s_sbd->s_state, only FSTYPE_SYSV */

    /* The following superblock entities don't change. */

    u32 s_firstinodezone;                 /* index of first inode zone */
    u32 s_firstdatazone;                  /* same as s_sbd->s_isize */
    u32 s_ninodes;                        /* total number of inodes */
    u32 s_ndatazones;                     /* total number of data zones */
    u32 s_nzones;                         /* same as s_sbd->s_fsize */
    u16 s_namelen;                        /* max length of dir entry */
    int s_forced_ro;
};

static inline struct sysv_sb_info*
SYSV_SB(struct super_block* sb)
{
    return (struct sysv_sb_info*)sb->s_fs_info;
}

static inline void
sysv_read3byte(struct sysv_sb_info* sbi, unsigned char* from, unsigned char* to)
{
    if (sbi->s_bytesex == UNIXFS_FS_PDP) {
        to[0] = from[0];
        to[1] = 0;
        to[2] = from[1];
        to[3] = from[2];
    } else if (sbi->s_bytesex == UNIXFS_FS_LITTLE) {
        to[0] = from[0];
        to[1] = from[1];
        to[2] = from[2];
        to[3] = 0;
    } else {
        to[0] = 0;
        to[1] = from[0];
        to[2] = from[1];
        to[3] = from[2];
    }
}

/* in-memory file system type identifiers */
enum {
    FSTYPE_NONE = 0,
    FSTYPE_XENIX,
    FSTYPE_SYSV4,
    FSTYPE_SYSV2,
    FSTYPE_COH,
    FSTYPE_V7,
    FSTYPE_AFS,
    FSTYPE_END,
};

#define SYSV_MAGIC_BASE    0x012FF7B3
#define XENIX_SUPER_MAGIC (SYSV_MAGIC_BASE + FSTYPE_XENIX)
#define SYSV4_SUPER_MAGIC (SYSV_MAGIC_BASE + FSTYPE_SYSV4)
#define SYSV2_SUPER_MAGIC (SYSV_MAGIC_BASE + FSTYPE_SYSV2)
#define COH_SUPER_MAGIC   (SYSV_MAGIC_BASE + FSTYPE_COH)

/* admissible values for i_nlink: 0.._LINK_MAX */
enum {
    XENIX_LINK_MAX = 126,  /* ?? */
    SYSV_LINK_MAX  = 126,  /* 127? 251? */
    V7_LINK_MAX    = 126,  /* ?? */
    COH_LINK_MAX   = 10000,
};

/*
 * Xenix
 */

#define XENIX_NICINOD 100 /* number of inode cache entries */
#define XENIX_NICFREE 100 /* number of free block list chunk entries */

struct xenix_super_block { /* on disk */
    __fs16      s_isize;    /* index of first data zone */
    __fs32      s_fsize __packed2__;    /* total number of zones of this fs */
    __fs16      s_nfree;    /* # of free blocks in s_free, <= XENIX_NICFREE */
    sysv_zone_t s_free[XENIX_NICFREE];  /* first free block list chunk */
    __fs16      s_ninode;   /* # of free inodes in s_inode, <= XENIX_NICINOD */
    sysv_ino_t  s_inode[XENIX_NICINOD]; /* some free inodes */
    char        s_flock;    /* lock during free block list manipulation */
    char        s_ilock;    /* lock during inode cache manipulation */
    char        s_fmod;     /* super-block modified flag */
    char        s_ronly;    /* flag whether fs is mounted read-only */
    __fs32      s_time __packed2__;     /* time of last super block update */
    __fs32      s_tfree __packed2__;    /* total number of free zones */
    __fs16      s_tinode;   /* total number of free inodes */
    __fs16      s_dinfo[4]; /* device information ?? */
    char        s_fname[6]; /* file system volume name */
    char        s_fpack[6]; /* file system pack name */
    char        s_clean;    /* set to 0x46 when fs was properly unmounted */
    char        s_fill[371];
    s32         s_magic;    /* version of file system */
    __fs32      s_type   ;  /* type of file system:
                               1 for 512 byte blocks 
                               2 for 1024 byte blocks
                               3 for 2048 byte blocks */
                                
};

/*
 * System V
 */

/*
 * System V FS comes in two variants:
 * sysv2: System V Release 2 (e.g. Microport), structure elements aligned(2).
 * sysv4: System V Release 4 (e.g. Consensys), structure elements aligned(4).
 */

#define SYSV_NICINOD 100    /* number of inode cache entries */
#define SYSV_NICFREE 50     /* number of free block list chunk entries */

struct sysv4_super_block {  /* on disk */
    __fs16      s_isize;    /* index of first data zone */
    u16         s_pad0;
    __fs32      s_fsize;    /* total number of zones of this fs */
    __fs16      s_nfree;    /* # of free blocks in s_free, <= SYSV_NICFREE */
    u16         s_pad1;
    sysv_zone_t s_free[SYSV_NICFREE]; /* first free block list chunk */
    __fs16      s_ninode;   /* # of free inodes in s_inode, <= SYSV_NICINOD */
    u16         s_pad2;
    sysv_ino_t  s_inode[SYSV_NICINOD]; /* some free inodes */
    char        s_flock;    /* lock during free block list manipulation */
    char        s_ilock;    /* lock during inode cache manipulation */
    char        s_fmod;     /* super-block modified flag */
    char        s_ronly;    /* flag whether fs is mounted read-only */
    __fs32      s_time;     /* time of last super block update */
    __fs16      s_dinfo[4]; /* device information ?? */
    __fs32      s_tfree;    /* total number of free zones */
    __fs16      s_tinode;   /* total number of free inodes */
    u16         s_pad3;
    char        s_fname[6]; /* file system volume name */
    char        s_fpack[6]; /* file system pack name */
    s32         s_fill[12];
    __fs32      s_state;    /* state: 0x7c269d38-s_time means it is clean */
    s32         s_magic;    /* version of file system */
    __fs32      s_type;     /* type of file system:
                                1 for 512 byte blocks
                                2 for 1024 byte blocks */
};

struct sysv2_super_block {    /* on disk */
    __fs16     s_isize;       /* index of first data zone */
    __fs32     s_fsize __packed2__;   /* total number of zones of this fs */
    __fs16     s_nfree;       /* # of free blocks in s_free, <= SYSV_NICFREE */
    sysv_zone_t s_free[SYSV_NICFREE]; /* first free block list chunk */
    __fs16     s_ninode;      /* # of free inodes in s_inode, <= SYSV_NICINOD */
    sysv_ino_t s_inode[SYSV_NICINOD]; /* some free inodes */
    char        s_flock;      /* lock during free block list manipulation */
    char        s_ilock;      /* lock during inode cache manipulation */
    char        s_fmod;       /* super-block modified flag */
    char        s_ronly;      /* flag whether fs is mounted read-only */
    __fs32      s_time __packed2__;   /* time of last super block update */
    __fs16      s_dinfo[4];   /* device information ?? */
    __fs32      s_tfree __packed2__;  /* total number of free zones */
    __fs16      s_tinode;     /* total number of free inodes */
    char        s_fname[6];   /* file system volume name */
    char        s_fpack[6];   /* file system pack name */
    s32         s_fill[14];
    __fs32      s_state;      /* file system state: 0xcb096f43 means clean */
    s32         s_magic;      /* version of file system */
    __fs32      s_type;       /* type of file system:
                                 1 for 512 byte blocks
                                 2 for 1024 byte blocks */
};

/*
 * Coherent
 */

#define COH_NICINOD 100 /* number of inode cache entries */
#define COH_NICFREE 64  /* number of free block list chunk entries */

struct coh_super_block {     /* on disk */
    __fs16      s_isize;     /* index of first data zone */
    __fs32      s_fsize __packed2__; /* total number of zones of this fs */
    __fs16      s_nfree;     /* # of free blocks in s_free, <= COH_NICFREE */
    sysv_zone_t s_free[COH_NICFREE] __packed2__; /* 1st free block list chunk */
    __fs16      s_ninode;    /* # of free inodes in s_inode, <= COH_NICINOD */
    sysv_ino_t  s_inode[COH_NICINOD]; /* some free inodes */
    char        s_flock;     /* lock during free block list manipulation */
    char        s_ilock;             /* lock during inode cache manipulation */
    char        s_fmod;              /* super-block modified flag */
    char        s_ronly;             /* flag whether fs is mounted read-only */
    __fs32      s_time __packed2__;  /* time of last super block update */
    __fs32      s_tfree __packed2__; /* total number of free zones */
    __fs16      s_tinode;            /* total number of free inodes */
    __fs16      s_interleave_m;      /* interleave factor */
    __fs16      s_interleave_n;
    char        s_fname[6];          /* file system volume name */
    char        s_fpack[6];          /* file system pack name */
    __fs32      s_unique;            /* zero, not used */
};

struct super_block* sysv_fill_super(int fd, void* args, int silent);
const char* sysv_flavor(u_int type);
u_long sysv_count_free_blocks(struct super_block* sb);
u_long sysv_count_free_inodes(struct super_block* sb);

struct sysv_dinode* sysv_raw_inode(struct super_block* sb, ino_t ino,
                                   struct buffer_head* bh);
int sysv_next_direntry(struct inode* dp, struct unixfs_dirbuf* dirbuf,
                       off_t* offset, struct unixfs_direntry* dent);

int sysv_get_block(struct inode* ip, sector_t block, off_t* result);
int sysv_get_page(struct inode* ip, sector_t index, char* pagebuf);

#endif /* _SYSVFS_H_ */
