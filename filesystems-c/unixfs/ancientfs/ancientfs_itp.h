/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_ITP_H_
#define _ANCIENTFS_ITP_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define BSIZE   512

#define ROOTINO  1
#define DIRSIZ   14
#define PATHSIZ  48

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    struct inode* s_rootip;
};

struct dinode_itp { /* newer */
    uint8_t  di_path[PATHSIZ]; /* if di_path[0] == 0, the entry is empty */
    uint16_t di_mode;       /* type and permissions */
    uint8_t  di_uid;        /* owner's id */
    uint8_t  di_gid;        /* group's id */
    uint8_t  di_unused1;    /* unused */
    uint8_t  di_size0;      /* size */
    uint16_t di_size1;      /* size */
    uint32_t di_mtime;      /* file's modification time */
    uint16_t di_addr;       /* tape block of the start of the file's contents */
    uint16_t di_cksum;      /* sum of the 32 words of the directory is 0 */
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

#include <sys/stat.h>

static inline mode_t
ancientfs_itp_mode(mode_t mode, uint32_t flags)
{
    mode_t newmode = 0;

    newmode = (int)mode & ~(IALLOC | ILARG);
    if ((newmode & IFMT) == 0) /* ancient regular file */
        newmode |= S_IFREG;
    return newmode;
}

static inline int
ancientfs_itp_cksum(uint8_t* de, fs_endian_t e, uint32_t flags)
{
    uint16_t* p = (uint16_t*)de;
    uint16_t cksum = 0;
    int i = 0;
    for (i = 0; i < 32; i++, p++)
        cksum += fs16_to_host(e, *p);
    return cksum;
}

#endif /* _ANCIENTFS_ITP_H_ */
