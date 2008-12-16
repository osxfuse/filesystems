/*
 * Minix File System Famiy for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of the minix file system. See fs/minix/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */

#include <linux/buffer_head.h>
#include "minixfs.h"

enum { DIRECT = 7, DEPTH = 4 };    /* Have triple indirect */

typedef u32 block_t;    /* 32 bit, host order */

static inline unsigned long block_to_cpu(block_t n)
{
    return n;
}

static inline block_t cpu_to_block(unsigned long n)
{
    return n;
}

static inline block_t* i_data(struct inode* inode)
{
    return (block_t*)minix_i(inode)->u.i2_data;
}

static int block_to_path(struct inode* inode, long block, int offsets[DEPTH])
{
    int n = 0;
    struct super_block* sb = inode->I_sb;

    if (block < 0) {
        printk("MINIX-fs: block_to_path: block %ld < 0\n", block);
    } else if (block >= (minix_sb(inode->I_sb)->s_max_size/sb->s_blocksize)) {
        if (0)
            printk("MINIX-fs: block_to_path: block %ld too big\n", block);
    } else if (block < 7) {
        offsets[n++] = block;
    } else if ((block -= 7) < 256) {
        offsets[n++] = 7;
        offsets[n++] = block;
    } else if ((block -= 256) < 256 * 256) {
        offsets[n++] = 8;
        offsets[n++] = block >> 8;
        offsets[n++] = block & 255;
    } else {
        block -= 256 * 256;
        offsets[n++] = 9;
        offsets[n++] = block >> 16;
        offsets[n++] = (block >> 8) & 255;
        offsets[n++] = block & 255;
    }
    return n;
}

#include "itree_common.c"

int
minix_get_block_v2(struct inode* inode, sector_t iblock, off_t* result) 
{
    return get_block(inode, iblock, result);
}

unsigned V2_minix_blocks(loff_t size, struct super_block* sb)
{
    return nblocks(size, sb);
}
