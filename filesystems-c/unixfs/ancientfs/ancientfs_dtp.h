/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_DTP_H_
#define _ANCIENTFS_DTP_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define BSIZE   512

#define ROOTINO  1
#define DIRSIZ   14
#define PATHSIZ  114

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    uint32_t s_dataoffset;
    struct inode* s_rootip;
};

struct dinode_dtp { /* newer */
    uint8_t  di_path[PATHSIZ]; /* if di_path[0] == 0, the entry is empty */
    uint16_t di_mode;       /* type and permissions */
    uint8_t  di_uid;        /* owner's id */
    uint8_t  di_gid;        /* group's id */
    uint8_t  di_unused1;    /* unused */
    uint8_t  di_size0;      /* size */
    uint16_t di_size1;      /* size */
    uint32_t di_mtime;      /* file's modification time */
    uint16_t di_addr;       /* tape block of the start of the file's contents */
} __attribute__((packed));

#define INOPB 4

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
#define ISUID   000040  /* set user ID on execution */
#define IEXEC   000020  /* executable */
#define IRUSR   000010  /* read, owner */
#define IWUSR   000004  /* write, owner */
#define IROTH   000002  /* read, non-owner */
#define IWOTH   000001  /* write, non-owner */

#include <sys/stat.h>

static inline mode_t
ancientfs_dtp_mode(mode_t mode, uint32_t flags)
{
    mode_t newmode = 0;

    newmode = (int)mode & ~(IALLOC | ILARG);
    if ((newmode & IFMT) == 0) /* ancient regular file */
        newmode |= S_IFREG;
    return newmode;
}

#endif /* _ANCIENTFS_DTP_H_ */
