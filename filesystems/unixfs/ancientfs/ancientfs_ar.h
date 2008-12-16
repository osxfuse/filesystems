/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_AR_H_
#define _ANCIENTFS_AR_H_

#include "unixfs_internal.h"
#include "ancientfs.h"

#define BSIZE   512
#define ROOTINO 1

struct filsys
{
    uint32_t s_fsize;
    uint32_t s_files;
    uint32_t s_directories;
    uint32_t s_lastino;
    struct inode* s_rootip;
};

#define ARMAG    "!<arch>\n" /* ar "magic number" */
#define SARMAG   8           /* strlen(ARMAG); */

#define AR_EFMT1 "#1/"       /* extended format #1 */

struct ar_hdr
{
    char ar_name[16];        /* ASCII name */
    char ar_date[12];        /* ASCII modification time */
    char ar_uid[6];          /* ASCII user id */
    char ar_gid[6];          /* ASCII group id */
    char ar_mode[8];         /* ASCII octal file permissions */
    char ar_size[10];        /* ASCII size in bytes */
#define ARFMAG  "`\n"
    char ar_fmag[2];         /* ASCII consistency check */
};

struct ar_node_info
{ 
    struct   inode*        ar_self;
    struct   ar_node_info* ar_parent;
    struct   ar_node_info* ar_children;
    struct   ar_node_info* ar_next_sibling;
    char*    ar_name;
    uint32_t ar_namelen;
};

#endif /* _ANCIENTFS_AR_H_ */
