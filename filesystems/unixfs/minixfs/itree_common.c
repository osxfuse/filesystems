/* Generic part */

typedef struct {
    block_t* p;
    block_t  key;
    struct buffer_head* bh;
} Indirect;

static inline void add_chain(Indirect* p, struct buffer_head* bh, block_t* v)
{
    p->key = *(p->p = v);
    p->bh = bh;
}

static inline int verify_chain(Indirect* from, Indirect* to)
{
    while (from <= to && from->key == *from->p)
        from++;
    return (from > to);
}

static inline block_t* block_end(struct buffer_head *bh)
{
    return (block_t*)((char*)bh->b_data + bh->b_size);
}

static inline Indirect*
get_branch(struct inode* inode, int depth, int* offsets, Indirect chain[DEPTH],
           int* err)
{
    struct super_block* sb = inode->I_sb;
    Indirect* p = chain;
    struct buffer_head *bh;

    *err = 0;
    /* i_data is not going away, no lock needed */
    add_chain (chain, NULL, i_data(inode) + *offsets);
    if (!p->key)
        goto no_block;

    while (--depth) {
        bh = malloc(sizeof(struct buffer_head));
        if (sb_bread_intobh(sb, block_to_cpu(p->key), bh) != 0)
            goto failure;
        if (!verify_chain(chain, p))
            goto changed;
        add_chain(++p, bh, (block_t *)bh->b_data + *++offsets);
        if (!p->key)
            goto no_block;
    }
    return NULL;

changed:
    brelse(bh);
    *err = -EAGAIN;
    goto no_block;

failure:
    *err = -EIO;

no_block:
    return p;
}

static inline int get_block(struct inode* inode, sector_t iblock, off_t* result)
{
    *result = (off_t)0;

    int err = -EIO;
    int offsets[DEPTH];
    Indirect chain[DEPTH];
    Indirect* partial;
    
    int depth = block_to_path(inode, iblock, offsets);

    if (depth == 0)
        goto out;

/* reread: */ 
    partial = get_branch(inode, depth, offsets, chain, &err);
    
    /* simplest case - block found, no allocation needed */
    if (!partial) {
        *result = (off_t)(block_to_cpu(chain[depth-1].key));
        /* clean up and exit */
        partial = chain + depth - 1; /* the whole chain */
        goto cleanup;
    }       
            
    /* Next simple case - plain lookup or failed read of indirect block */
cleanup:        
    while (partial > chain) {
        brelse(partial->bh);
        partial--;
    }

out:
    return err;
}

static inline int all_zeroes(block_t* p, block_t* q)
{
    while (p < q)
        if (*p++)
            return 0;

    return 1;
}

static inline unsigned nblocks(loff_t size, struct super_block* sb)
{
    int k = sb->s_blocksize_bits - 10;
    unsigned blocks, res, direct = DIRECT, i = DEPTH;
    blocks = (size + sb->s_blocksize - 1) >> (BLOCK_SIZE_BITS + k);
    res = blocks;
    while (--i && blocks > direct) {
        blocks -= direct;
        blocks += sb->s_blocksize/sizeof(block_t) - 1;
        blocks /= sb->s_blocksize/sizeof(block_t);
        res += blocks;
        direct = 1;
    }

    return res;
}
