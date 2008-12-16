/*
 * UFS for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _UFS_H_
#define _UFS_H_

#include "unixfs_internal.h"
#include "linux.h"

// #define CONFIG_UFS_DEBUG 1

#include <ufs/ufs_fs.h>
#include <ufs/ufs.h>
#include <ufs/swab.h>
#include <linux/parser.h>

struct super_block*
      U_ufs_fill_super(int fd, void* args, int silent);
int   U_ufs_statvfs(struct super_block* sb, struct statvfs* buf);
int   U_ufs_iget(struct super_block* sb, struct inode* ip);
ino_t U_ufs_inode_by_name(struct inode* dir, const char* name);
int   U_ufs_next_direntry(struct inode* dir, struct unixfs_dirbuf* dirbuf,
                          off_t* offset, struct unixfs_direntry* dent);
int   U_ufs_get_block(struct inode* ip, sector_t fragment, off_t* result);
int   U_ufs_get_page(struct inode* ip, sector_t index, char* pagebuf);

#endif /* _UFS_H_ */
