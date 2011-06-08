#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>

#include "unixfs_internal.h"
#include "linux.h"

int
sb_bread_intobh(struct super_block* sb, off_t block, struct buffer_head* bh)
{
    if (pread(sb->s_bdev, bh->b_data, sb->s_blocksize,
              block * (off_t)sb->s_blocksize) != sb->s_blocksize)
        return EIO;

    return 0;
}

void
brelse(struct buffer_head* bh)
{
    if (bh && bh->b_flags.dynamic)
        free((void*)bh);
}

struct buffer_head*
sb_getblk(struct super_block* sb, sector_t block)
{
    struct buffer_head* bh = calloc(1, sizeof(struct buffer_head));
    if (!bh) {
        fprintf(stderr, "*** fatal error: cannot allocate buffer\n");
        abort();
    }
    bh->b_flags.dynamic = 1;
    bh->b_size = PAGE_SIZE;
    bh->b_blocknr = block;
    return bh;
}

struct buffer_head*
sb_bread(struct super_block* sb, off_t block)
{
    struct buffer_head* bh = sb_getblk(sb, block);
    if (bh && sb_bread_intobh(sb, block, bh) != 0) {
        brelse(bh);
        return NULL;
    }
    return bh;
}
