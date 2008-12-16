/*
 * UFS for MacFUSE
 * Amit Singh
 * http://osxbook.com
 *
 * Most of the code in this file comes from the Linux kernel implementation
 * of UFS. See fs/ufs/ in the Linux kernel source tree.
 *
 * The code is Copyright (c) its various authors. It is covered by the
 * GNU GENERAL PUBLIC LICENSE Version 2.
 */
 
#include "ufs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <ufs/util.c>

static ino_t ufs_find_entry_s(struct inode *dir, const char* name);
static int ufs_get_dirpage(struct inode *inode, sector_t index, char *pagebuf);
static int ufs_read_cylinder_structures(struct super_block *sb);
static void ufs_print_cylinder_stuff(struct super_block* sb,
                                     struct ufs_cylinder_group* cg);
static void ufs_setup_cstotal(struct super_block* sb);
static int ufs_read_cylinder_structures(struct super_block* sb);
static u64 ufs_frag_map(struct inode *inode, sector_t frag, int* error);
int ufs_getfrag_block(struct inode* inode, sector_t fragment,
                      struct buffer_head* bh_result, int create);
static struct buffer_head* ufs_getfrag(struct inode* inode,
                                       unsigned int fragment,
                                       int create, int* err);
struct buffer_head* ufs_bread(struct inode* inode, unsigned fragment,
                              int create, int* err);
static int ufs1_read_inode(struct inode* inode, struct ufs_inode* ufs_inode);
static int ufs2_read_inode(struct inode* inode, struct ufs2_inode* ufs2_inode);
static unsigned ufs_last_byte(struct inode* inode, unsigned long page_nr);
static int ufs_get_dirpage(struct inode* inode, sector_t index, char* pagebuf);
static ino_t ufs_find_entry_s(struct inode* dir, const char* name);

#define UFS_USED __attribute__((used))
static struct ufs_dir_entry*
    ufs_dotdot_s(struct inode* dir, char* pagebuf) UFS_USED;
static int
    ufs_check_page(struct inode* dir, sector_t index, char* page) UFS_USED;
static void
    ufs_print_super_stuff(struct super_block* sb,
        struct ufs_super_block_first*  usb1,
        struct ufs_super_block_second* usb2,
        struct ufs_super_block_third*  usb3) UFS_USED;

enum {
   Opt_type_old        = UFS_MOUNT_UFSTYPE_OLD,
   Opt_type_sunx86     = UFS_MOUNT_UFSTYPE_SUNx86,
   Opt_type_sun        = UFS_MOUNT_UFSTYPE_SUN,
   Opt_type_sunos      = UFS_MOUNT_UFSTYPE_SUNOS,
   Opt_type_44bsd      = UFS_MOUNT_UFSTYPE_44BSD,
   Opt_type_ufs2       = UFS_MOUNT_UFSTYPE_UFS2,
   Opt_type_hp         = UFS_MOUNT_UFSTYPE_HP,
   Opt_type_nextstepcd = UFS_MOUNT_UFSTYPE_NEXTSTEP_CD,
   Opt_type_nextstep   = UFS_MOUNT_UFSTYPE_NEXTSTEP,
   Opt_type_openstep   = UFS_MOUNT_UFSTYPE_OPENSTEP,
   Opt_onerror_panic   = UFS_MOUNT_ONERROR_PANIC,
   Opt_onerror_lock    = UFS_MOUNT_ONERROR_LOCK,
   Opt_onerror_umount  = UFS_MOUNT_ONERROR_UMOUNT,
   Opt_onerror_repair  = UFS_MOUNT_ONERROR_REPAIR,
   Opt_err
};

static match_table_t tokens __attribute__((used)) = {
    { Opt_type_old,        "old"         },
    { Opt_type_sunx86,     "sunx86"      },
    { Opt_type_sun,        "sun"         },
    { Opt_type_sunos,      "sunos"       },
    { Opt_type_44bsd,      "44bsd"       },
    { Opt_type_ufs2,       "ufs2"        },
    { Opt_type_ufs2,       "5xbsd"       },
    { Opt_type_hp,         "hp"          },
    { Opt_type_nextstepcd, "nextstep-cd" },
    { Opt_type_nextstep,   "nextstep"    },
    { Opt_type_openstep,   "openstep"    },

    /* end of possible ufs types */
    { Opt_onerror_panic,   "onerror=panic"  },
    { Opt_onerror_lock,    "onerror=lock"   },
    { Opt_onerror_umount,  "onerror=umount" },
    { Opt_onerror_repair,  "onerror=repair" },
    { Opt_err, NULL}
};

static void
ufs_print_super_stuff(struct super_block* sb,
                      struct ufs_super_block_first*  usb1,
                      struct ufs_super_block_second* usb2,
                      struct ufs_super_block_third*  usb3)
{
    u32 magic = fs32_to_cpu(sb, usb3->fs_magic);

    printk("ufs_print_super_stuff\n");
    printk("  magic:     0x%x\n", magic);
    if (fs32_to_cpu(sb, usb3->fs_magic) == UFS2_MAGIC) {
        printk("  fs_size:   %llu\n", (unsigned long long)
               fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size));
        printk("  fs_dsize:  %llu\n", (unsigned long long)
               fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize));
        printk("  bsize:         %u\n",
               fs32_to_cpu(sb, usb1->fs_bsize));
        printk("  fsize:         %u\n",
               fs32_to_cpu(sb, usb1->fs_fsize));
        printk("  fs_volname:  %s\n", usb2->fs_un.fs_u2.fs_volname);
        printk("  fs_sblockloc: %llu\n", (unsigned long long)
               fs64_to_cpu(sb, usb2->fs_un.fs_u2.fs_sblockloc));
        printk("  cs_ndir(No of dirs):  %llu\n", (unsigned long long)
               fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_ndir));
        printk("  cs_nbfree(No of free blocks):  %llu\n",
               (unsigned long long)
               fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_nbfree));
        printk(KERN_INFO"  cs_nifree(Num of free inodes): %llu\n",
               (unsigned long long)
               fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nifree));
        printk(KERN_INFO"  cs_nffree(Num of free frags): %llu\n",
               (unsigned long long)
               fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nffree));
        printk(KERN_INFO"  fs_maxsymlinklen: %u\n",
               fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_maxsymlinklen));
    } else {
        printk(" sblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_sblkno));
        printk(" cblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_cblkno));
        printk(" iblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_iblkno));
        printk(" dblkno:      %u\n", fs32_to_cpu(sb, usb1->fs_dblkno));
        printk(" cgoffset:    %u\n",
               fs32_to_cpu(sb, usb1->fs_cgoffset));
        printk(" ~cgmask:     0x%x\n",
               ~fs32_to_cpu(sb, usb1->fs_cgmask));
        printk(" size:        %u\n", fs32_to_cpu(sb, usb1->fs_size));
        printk(" dsize:       %u\n", fs32_to_cpu(sb, usb1->fs_dsize));
        printk(" ncg:         %u\n", fs32_to_cpu(sb, usb1->fs_ncg));
        printk(" bsize:       %u\n", fs32_to_cpu(sb, usb1->fs_bsize));
        printk(" fsize:       %u\n", fs32_to_cpu(sb, usb1->fs_fsize));
        printk(" frag:        %u\n", fs32_to_cpu(sb, usb1->fs_frag));
        printk(" fragshift:   %u\n",
               fs32_to_cpu(sb, usb1->fs_fragshift));
        printk(" ~fmask:      %u\n", ~fs32_to_cpu(sb, usb1->fs_fmask));
        printk(" fshift:      %u\n", fs32_to_cpu(sb, usb1->fs_fshift));
        printk(" sbsize:      %u\n", fs32_to_cpu(sb, usb1->fs_sbsize));
        printk(" spc:         %u\n", fs32_to_cpu(sb, usb1->fs_spc));
        printk(" cpg:         %u\n", fs32_to_cpu(sb, usb1->fs_cpg));
        printk(" ipg:         %u\n", fs32_to_cpu(sb, usb1->fs_ipg));
        printk(" fpg:         %u\n", fs32_to_cpu(sb, usb1->fs_fpg));
        printk(" csaddr:      %u\n", fs32_to_cpu(sb, usb1->fs_csaddr));
        printk(" cssize:      %u\n", fs32_to_cpu(sb, usb1->fs_cssize));
        printk(" cgsize:      %u\n", fs32_to_cpu(sb, usb1->fs_cgsize));
        printk(" fstodb:      %u\n",
               fs32_to_cpu(sb, usb1->fs_fsbtodb));
        printk(" nrpos:       %u\n", fs32_to_cpu(sb, usb3->fs_nrpos));
        printk(" ndir         %u\n",
               fs32_to_cpu(sb, usb1->fs_cstotal.cs_ndir));
        printk(" nifree       %u\n",
               fs32_to_cpu(sb, usb1->fs_cstotal.cs_nifree));
        printk(" nbfree       %u\n",
               fs32_to_cpu(sb, usb1->fs_cstotal.cs_nbfree));
        printk(" nffree       %u\n",
               fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree));
    }
    printk("\n");
}

static void
ufs_print_cylinder_stuff(struct super_block* sb, struct ufs_cylinder_group* cg)
{
    printk("\nufs_print_cylinder_stuff\n");
    printk("size of ucg: %zu\n", sizeof(struct ufs_cylinder_group));
    printk("  magic:        %x\n", fs32_to_cpu(sb, cg->cg_magic));
    printk("  time:         %u\n", fs32_to_cpu(sb, cg->cg_time));
    printk("  cgx:          %u\n", fs32_to_cpu(sb, cg->cg_cgx));
    printk("  ncyl:         %u\n", fs16_to_cpu(sb, cg->cg_ncyl));
    printk("  niblk:        %u\n", fs16_to_cpu(sb, cg->cg_niblk));
    printk("  ndblk:        %u\n", fs32_to_cpu(sb, cg->cg_ndblk));
    printk("  cs_ndir:      %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_ndir));
    printk("  cs_nbfree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nbfree));
    printk("  cs_nifree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nifree));
    printk("  cs_nffree:    %u\n", fs32_to_cpu(sb, cg->cg_cs.cs_nffree));
    printk("  rotor:        %u\n", fs32_to_cpu(sb, cg->cg_rotor));
    printk("  frotor:       %u\n", fs32_to_cpu(sb, cg->cg_frotor));
    printk("  irotor:       %u\n", fs32_to_cpu(sb, cg->cg_irotor));
    printk("  frsum:        %u, %u, %u, %u, %u, %u, %u, %u\n",
           fs32_to_cpu(sb, cg->cg_frsum[0]), fs32_to_cpu(sb, cg->cg_frsum[1]),
           fs32_to_cpu(sb, cg->cg_frsum[2]), fs32_to_cpu(sb, cg->cg_frsum[3]),
           fs32_to_cpu(sb, cg->cg_frsum[4]), fs32_to_cpu(sb, cg->cg_frsum[5]),
           fs32_to_cpu(sb, cg->cg_frsum[6]), fs32_to_cpu(sb, cg->cg_frsum[7]));
    printk("  btotoff:      %u\n", fs32_to_cpu(sb, cg->cg_btotoff));
    printk("  boff:         %u\n", fs32_to_cpu(sb, cg->cg_boff));
    printk("  iuseoff:      %u\n", fs32_to_cpu(sb, cg->cg_iusedoff));
    printk("  freeoff:      %u\n", fs32_to_cpu(sb, cg->cg_freeoff));
    printk("  nextfreeoff:  %u\n", fs32_to_cpu(sb, cg->cg_nextfreeoff));
    printk("  clustersumoff %u\n",
           fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clustersumoff));
    printk("  clusteroff    %u\n",
           fs32_to_cpu(sb, cg->cg_u.cg_44.cg_clusteroff));
    printk("  nclusterblks  %u\n",
           fs32_to_cpu(sb, cg->cg_u.cg_44.cg_nclusterblks));
    printk("\n");
}

static int
ufs_parse_options(char* options, unsigned* mount_options)
{
    char* p;
    
    UFSD("ENTER\n");
    
    if (!options)
        return 1;

    while ((p = strsep(&options, ",")) != NULL) {

        substring_t args[MAX_OPT_ARGS];
        int token;
        if (!*p)
            continue;

        token = match_token(p, tokens, args);

        switch (token) {
        case Opt_type_old:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_OLD);
            break;

        case Opt_type_sunx86:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_SUNx86);
            break;

        case Opt_type_sun:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_SUN);
            break;

        case Opt_type_sunos:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_SUNOS);
            break;

        case Opt_type_44bsd:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_44BSD);
            break;

        case Opt_type_ufs2:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_UFS2);
            break;

        case Opt_type_hp:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_HP);
            break;

        case Opt_type_nextstepcd:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_NEXTSTEP_CD);
            break;

        case Opt_type_nextstep:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_NEXTSTEP);
            break;

        case Opt_type_openstep:
            ufs_clear_opt(*mount_options, UFSTYPE);
            ufs_set_opt(*mount_options, UFSTYPE_OPENSTEP);
            break;

        case Opt_onerror_panic:
            ufs_clear_opt(*mount_options, ONERROR);
            ufs_set_opt(*mount_options, ONERROR_PANIC);
            break;

        case Opt_onerror_lock:
            ufs_clear_opt(*mount_options, ONERROR);
            ufs_set_opt(*mount_options, ONERROR_LOCK);
            break;

        case Opt_onerror_umount:
            ufs_clear_opt(*mount_options, ONERROR);
            ufs_set_opt(*mount_options, ONERROR_UMOUNT);
            break;

        case Opt_onerror_repair:
            printk("UFS-fs: Unable to do repair on error, "
                   "will lock lock instead\n");
            ufs_clear_opt(*mount_options, ONERROR);
            ufs_set_opt(*mount_options, ONERROR_REPAIR);
            break;

        default:
            printk("UFS-fs: Invalid option: \"%s\" or missing value\n", p);
            return 0;
        }
    }

    return 1;
}

static void
ufs_setup_cstotal(struct super_block* sb)
{
    struct ufs_sb_info* sbi = UFS_SB(sb);
    struct ufs_sb_private_info* uspi = sbi->s_uspi;

    struct ufs_super_block_first*  usb1;
    struct ufs_super_block_second* usb2;
    struct ufs_super_block_third*  usb3;

    unsigned mtype = sbi->s_mount_opt & UFS_MOUNT_UFSTYPE;

    UFSD("ENTER, mtype=%u\n", mtype);

    usb1 = ubh_get_usb_first(uspi);
    usb2 = ubh_get_usb_second(uspi);
    usb3 = ubh_get_usb_third(uspi);

    if ((mtype == UFS_MOUNT_UFSTYPE_44BSD &&
        (usb1->fs_flags & UFS_FLAGS_UPDATED)) ||
        (mtype == UFS_MOUNT_UFSTYPE_UFS2)) {

        uspi->cs_total.cs_ndir = fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_ndir);
        uspi->cs_total.cs_nbfree = fs64_to_cpu(sb, usb2->fs_un.fs_u2.cs_nbfree);
        uspi->cs_total.cs_nifree =
            fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nifree);
        uspi->cs_total.cs_nffree =
            fs64_to_cpu(sb, usb3->fs_un1.fs_u2.cs_nffree);
    } else {
        uspi->cs_total.cs_ndir = fs32_to_cpu(sb, usb1->fs_cstotal.cs_ndir);
        uspi->cs_total.cs_nbfree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nbfree);
        uspi->cs_total.cs_nifree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nifree);
        uspi->cs_total.cs_nffree = fs32_to_cpu(sb, usb1->fs_cstotal.cs_nffree);
    }
    UFSD("EXIT\n");
}

static int
ufs_read_cylinder_structures(struct super_block* sb)
{
    struct ufs_sb_info* sbi = UFS_SB(sb);
    struct ufs_sb_private_info* uspi = sbi->s_uspi;

    struct ufs_buffer_head* ubh;
    unsigned char* base;
    unsigned char* space;
    unsigned size, blks, i;
    struct ufs_super_block_third* usb3;

    UFSD("ENTER\n");

    usb3 = ubh_get_usb_third(uspi);

    /* Read cs structures from (usually) first data block on the device. */

    size = uspi->s_cssize;
    blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
    base = space = kmalloc(size, GFP_KERNEL);
    if (!base)
        goto failed; 

    sbi->s_csp = (struct ufs_csum*)space;

    for (i = 0; i < blks; i += uspi->s_fpb) {
        size = uspi->s_bsize;
        if (i + uspi->s_fpb > blks)
            size = (blks - i) * uspi->s_fsize;

        ubh = ubh_bread(sb, uspi->s_csaddr + i, size);
        
        if (!ubh)
            goto failed;

        ubh_ubhcpymem (space, ubh, size);

        space += size;
        ubh_brelse (ubh);
        ubh = NULL;
    }

    /*
     * Read cylinder group (we read only first fragment from block
     * at this time) and prepare internal data structures for cg caching.
     */

    if (!(sbi->s_ucg = kmalloc(sizeof(struct buffer_head*) * uspi->s_ncg,
        GFP_KERNEL)))
        goto failed;

    for (i = 0; i < uspi->s_ncg; i++) 
        sbi->s_ucg[i] = NULL;

    for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
        sbi->s_ucpi[i] = NULL;
        sbi->s_cgno[i] = UFS_CGNO_EMPTY;
    }

    for (i = 0; i < uspi->s_ncg; i++) {
        UFSD("read cg %u\n", i);
        if (!(sbi->s_ucg[i] = sb_bread(sb, ufs_cgcmin(i))))
            goto failed;
        if (!ufs_cg_chkmagic(sb,
            (struct ufs_cylinder_group*)sbi->s_ucg[i]->b_data))
            goto failed;

        ufs_print_cylinder_stuff(sb,
            (struct ufs_cylinder_group*)sbi->s_ucg[i]->b_data);
    }

    for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
        if (!(sbi->s_ucpi[i] = kmalloc(sizeof(struct ufs_cg_private_info),
                                       GFP_KERNEL)))
            goto failed;
        sbi->s_cgno[i] = UFS_CGNO_EMPTY;
    }

    sbi->s_cg_loaded = 0;

    UFSD("EXIT\n");

    return 1;

failed:
    kfree(base);

    if (sbi->s_ucg) {
        for (i = 0; i < uspi->s_ncg; i++)
            if (sbi->s_ucg[i])
                brelse (sbi->s_ucg[i]);
        kfree (sbi->s_ucg);
        for (i = 0; i < UFS_MAX_GROUP_LOADED; i++)
            kfree (sbi->s_ucpi[i]);
    }

    UFSD("EXIT (FAILED)\n");

    return 0;
}

static int
ufs_block_to_path(struct inode* inode, sector_t i_block, sector_t offsets[4])
{
    struct ufs_sb_private_info* uspi = UFS_SB(inode->I_sb)->s_uspi;

    int ptrs = uspi->s_apb;
    int ptrs_bits = uspi->s_apbshift;
    const long direct_blocks = UFS_NDADDR,
    indirect_blocks = ptrs,
    double_blocks = (1 << (ptrs_bits * 2));
    int n = 0;

    UFSD("ptrs=uspi->s_apb = %d,double_blocks=%ld \n",ptrs,double_blocks);

    if (i_block < 0) {
        fprintf(stderr, "ufs_block_to_path: block < 0\n");
    } else if (i_block < direct_blocks) {
        offsets[n++] = i_block;
    } else if ((i_block -= direct_blocks) < indirect_blocks) {
        offsets[n++] = UFS_IND_BLOCK;
        offsets[n++] = i_block;
    } else if ((i_block -= indirect_blocks) < double_blocks) {
        offsets[n++] = UFS_DIND_BLOCK;
        offsets[n++] = i_block >> ptrs_bits;
        offsets[n++] = i_block & (ptrs - 1);
    } else if (((i_block -= double_blocks) >> (ptrs_bits * 2)) < ptrs) {
        offsets[n++] = UFS_TIND_BLOCK;
        offsets[n++] = i_block >> (ptrs_bits * 2);
        offsets[n++] = (i_block >> ptrs_bits) & (ptrs - 1);
        offsets[n++] = i_block & (ptrs - 1);
    } else {
         fprintf(stderr, "ufs_block_to_path: block > big\n");
    }

    return n;
}

/* Returns the location of the fragment from the begining of the filesystem. */

static u64
ufs_frag_map(struct inode* inode, sector_t frag, int* error)
{
    struct ufs_inode_info* ufsi = inode->I_private;
    struct super_block* sb = inode->I_sb;
    struct ufs_sb_private_info* uspi = UFS_SB(sb)->s_uspi;

    u64 mask = (u64)uspi->s_apbmask>>uspi->s_fpbshift;
    int shift = uspi->s_apbshift-uspi->s_fpbshift;
    unsigned flags = UFS_SB(sb)->s_flags;

    sector_t offsets[4];
    sector_t* p;

    int depth = ufs_block_to_path(inode, frag >> uspi->s_fpbshift, offsets);

    u64 ret = 0L;
    u64 temp = 0L;

    __fs32 block;
    __fs64 u2_block = 0L;

    UFSD(": frag = %llu  depth = %d\n", (unsigned long long)frag, depth);
    UFSD(": uspi->s_fpbshift = %d ,uspi->s_apbmask = %x, mask=%llx\n",
         uspi->s_fpbshift, uspi->s_apbmask, (unsigned long long)mask);

    if (depth == 0)
        return 0;

    p = offsets;

    lock_kernel();

    if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
        goto ufs2;

    block = ufsi->i_u1.i_data[*p++];
    if (!block)
        goto out;

    while (--depth) {

        struct buffer_head _bh;
        struct buffer_head* bh = &_bh;
        sector_t n = *p++;

        bh->b_flags.dynamic = 0;

        if (sb_bread_intobh(sb, uspi->s_sbbase +
                            fs32_to_cpu(sb, block) + (n >> shift), bh) != 0)
            goto out;

        block = ((__fs32 *) bh->b_data)[n & mask];

        brelse (bh);

        if (!block)
            goto out;
    }

    ret = (u64)(uspi->s_sbbase + fs32_to_cpu(sb, block) +
                (frag & uspi->s_fpbmask));

    goto out;

ufs2:

    u2_block = ufsi->i_u1.u2_i_data[*p++];
    if (!u2_block)
        goto out;

    while (--depth) {

        struct buffer_head _bh;
        struct buffer_head* bh = &_bh;
        sector_t n = *p++;

        temp = (u64)(uspi->s_sbbase) + fs64_to_cpu(sb, u2_block);

        bh->b_flags.dynamic = 0;
        if (sb_bread_intobh(sb, temp +(u64) (n>>shift), bh) != 0)
            goto out;

        u2_block = ((__fs64 *)bh->b_data)[n & mask];

        brelse(bh);

        if (!u2_block)
            goto out;
    }

    temp = (u64)uspi->s_sbbase + fs64_to_cpu(sb, u2_block);
    ret = temp + (u64)(frag & uspi->s_fpbmask);

out:
    unlock_kernel();

    return ret;
}

int
ufs_getfrag_block(struct inode* inode, sector_t fragment,
                  struct buffer_head* bh_result, int create)
{
    int err;
    u64 phys64 = 0;
   
    struct super_block* sb = inode->I_sb;

    if (!create) {
        phys64 = ufs_frag_map(inode, fragment, &err);
        UFSD("phys64 = %llu\n", (unsigned long long)phys64);
        if (phys64) {
            bh_result->b_blocknr = phys64; /* map_bh */
            bh_result->b_size = sb->s_blocksize; /* map_bh */
        }
        return 0;
    }

    return -1;
}

static struct buffer_head*
ufs_getfrag(struct inode* inode, unsigned int fragment, int create, int* err)
{
    struct buffer_head dummy;
    int error;

    dummy.b_blocknr = -1000;
    error = ufs_getfrag_block(inode, fragment, &dummy, create);
    *err = error;
    if (!error) {
        struct buffer_head *bh;
        bh = sb_getblk(inode->I_sb, dummy.b_blocknr);
        return bh;
    }
    return NULL;
}

/* We don't use this. */
struct buffer_head*
ufs_bread(struct inode* inode, unsigned fragment, int create, int* err)
{
    struct buffer_head* bh;

    UFSD("ENTER, ino %llu, fragment %u\n", inode->I_ino, fragment);

    bh = ufs_getfrag(inode, fragment, create, err);
    if (!bh)
        return bh;

    struct buffer_head* bhnew = sb_bread(inode->I_sb, bh->b_blocknr);

    brelse(bh);

    return bhnew;
}

static int
ufs1_read_inode(struct inode* inode, struct ufs_inode* ufs_inode)
{
    struct ufs_inode_info* ufsi = inode->I_private;
    struct super_block* sb = inode->I_sb;

    mode_t mode;
    unsigned i;

    inode->I_mode = mode = fs16_to_cpu(sb, ufs_inode->ui_mode);
    inode->I_nlink = fs16_to_cpu(sb, ufs_inode->ui_nlink);

    if (inode->I_nlink == 0) {
        fprintf(stderr,
                "ufs_read_inode: inode %llu has zero nlink\n", inode->I_ino);
        return -1;
    }
    
    inode->I_uid  = ufs_get_inode_uid(sb, ufs_inode);
    inode->I_gid  = ufs_get_inode_gid(sb, ufs_inode);
    inode->I_size = fs64_to_cpu(sb, ufs_inode->ui_size);
    inode->I_atime.tv_sec  = fs32_to_cpu(sb, ufs_inode->ui_atime.tv_sec);
    inode->I_ctime.tv_sec  = fs32_to_cpu(sb, ufs_inode->ui_ctime.tv_sec);
    inode->I_mtime.tv_sec  = fs32_to_cpu(sb, ufs_inode->ui_mtime.tv_sec);
    inode->I_mtime.tv_nsec = 0;
    inode->I_atime.tv_nsec = 0;
    inode->I_ctime.tv_nsec = 0;
    inode->I_blocks = fs32_to_cpu(sb, ufs_inode->ui_blocks);
    inode->I_generation = fs32_to_cpu(sb, ufs_inode->ui_gen);
    inode->I_flags = fs32_to_cpu(sb, ufs_inode->ui_flags); /* XXX */
    ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
    ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);
    
    if (S_ISCHR(mode) || S_ISBLK(mode) || inode->I_blocks) {
        for (i = 0; i < (UFS_NDADDR + UFS_NINDIR); i++)
            ufsi->i_u1.i_data[i] = ufs_inode->ui_u2.ui_addr.ui_db[i];
    } else {
        for (i = 0; i < (UFS_NDADDR + UFS_NINDIR) * 4; i++)
            ufsi->i_u1.i_symlink[i] = ufs_inode->ui_u2.ui_symlink[i];
    }

    return 0;
}

static int
ufs2_read_inode(struct inode* inode, struct ufs2_inode* ufs2_inode)
{
    struct ufs_inode_info* ufsi = inode->I_private;
    struct super_block* sb = inode->I_sb;

    mode_t mode;
    unsigned i;

    UFSD("Reading ufs2 inode, ino %llu\n", inode->I_ino);

    inode->I_mode = mode = fs16_to_cpu(sb, ufs2_inode->ui_mode);
    inode->I_nlink = fs16_to_cpu(sb, ufs2_inode->ui_nlink);
    if (inode->I_nlink == 0) {
        fprintf(stderr,
                "ufs_read_inode: inode %llu has zero nlink\n", inode->I_ino);
        return -1;
    }

    inode->I_uid  = fs32_to_cpu(sb, ufs2_inode->ui_uid);
    inode->I_gid  = fs32_to_cpu(sb, ufs2_inode->ui_gid);
    inode->I_size = fs64_to_cpu(sb, ufs2_inode->ui_size);
    inode->I_atime.tv_sec  = fs64_to_cpu(sb, ufs2_inode->ui_atime);
    inode->I_ctime.tv_sec  = fs64_to_cpu(sb, ufs2_inode->ui_ctime);
    inode->I_mtime.tv_sec  = fs64_to_cpu(sb, ufs2_inode->ui_mtime);
    inode->I_atime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_atimensec);
    inode->I_ctime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_ctimensec);
    inode->I_mtime.tv_nsec = fs32_to_cpu(sb, ufs2_inode->ui_mtimensec);
    inode->I_blocks = fs64_to_cpu(sb, ufs2_inode->ui_blocks);
    inode->I_generation = fs32_to_cpu(sb, ufs2_inode->ui_gen);
    ufsi->i_flags = fs32_to_cpu(sb, ufs2_inode->ui_flags);
    /*
     * ufsi->i_shadow = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_shadow);
     * ufsi->i_oeftflag = fs32_to_cpu(sb, ufs_inode->ui_u3.ui_sun.ui_oeftflag);
     */

    if (S_ISCHR(mode) || S_ISBLK(mode) || inode->I_blocks) {
        for (i = 0; i < (UFS_NDADDR + UFS_NINDIR); i++)
            ufsi->i_u1.u2_i_data[i] =
                ufs2_inode->ui_u2.ui_addr.ui_db[i];
    } else {
        for (i = 0; i < (UFS_NDADDR + UFS_NINDIR) * 4; i++)
            ufsi->i_u1.i_symlink[i] = ufs2_inode->ui_u2.ui_symlink[i];
    }

    return 0;
}

static inline int
ufs_match(struct super_block* sb, int len, const char* const name,
          struct ufs_dir_entry* de)
{
    if (len != ufs_get_de_namlen(sb, de))
        return 0;

    if (!de->d_ino)
        return 0;

    return !memcmp(name, de->d_name, len);
}

static inline struct ufs_dir_entry*
ufs_next_entry(struct super_block* sb, struct ufs_dir_entry* p)
{
    return (struct ufs_dir_entry*)((char*)p + fs16_to_cpu(sb, p->d_reclen));
}

static inline unsigned
ufs_validate_entry(struct super_block* sb, char* base, unsigned offset,
                   unsigned mask)
{
    struct ufs_dir_entry* de = (struct ufs_dir_entry*)(base + offset);
    struct ufs_dir_entry* p = (struct ufs_dir_entry*)(base + (offset & mask));

    while ((char*)p < (char*)de) {
        if (p->d_reclen == 0)
            break;
        p = ufs_next_entry(sb, p);
    }

    return (char*)p - base;
}

static inline unsigned long
ufs_dir_pages(struct inode *inode)
{
    return (inode->I_size + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

static int
ufs_check_page(struct inode* dir, sector_t index, char* page)
{
    struct super_block* sb = dir->I_sb;
    char* kaddr = (char*)page;
    unsigned offs, rec_len;
    unsigned limit = PAGE_CACHE_SIZE;
    const unsigned chunk_mask = UFS_SB(sb)->s_uspi->s_dirblksize - 1;
    struct ufs_dir_entry*p;
    char* error;

    int fres = -1;

    if ((dir->I_size >> PAGE_CACHE_SHIFT) == index) {

        limit = dir->I_size & ~PAGE_CACHE_MASK;

        if (limit & chunk_mask)
            goto Ebadsize;

        if (!limit)
            goto out;
    }

    for (offs = 0; offs <= limit - UFS_DIR_REC_LEN(1); offs += rec_len) {

        p = (struct ufs_dir_entry*)(kaddr + offs);
        rec_len = fs16_to_cpu(sb, p->d_reclen);

        if (rec_len < UFS_DIR_REC_LEN(1))
            goto Eshort;

        if (rec_len & 3)
            goto Ealign;

        if (rec_len < UFS_DIR_REC_LEN(ufs_get_de_namlen(sb, p)))
            goto Enamelen;

        if (((offs + rec_len - 1) ^ offs) & ~chunk_mask)
            goto Espan;

        if (fs32_to_cpu(sb, p->d_ino) > (UFS_SB(sb)->s_uspi->s_ipg *
                        UFS_SB(sb)->s_uspi->s_ncg))
            goto Einumber;
    }

    if (offs != limit)
        goto Eend;

out:
    return 0; /* good */

Ebadsize:
    fprintf(stderr, "ufs_check_page: size of directory #%llu is "
            "not a multiple of chunk size", dir->I_ino);
    goto fail;

Eshort:
    error = "rec_len is smaller than minimal";
    goto bad_entry;

Ealign:
    error = "unaligned directory entry";
    goto bad_entry;

Enamelen:
    error = "rec_len is too small for name_len";
    goto bad_entry;

Espan:
    error = "directory entry across blocks";
    goto bad_entry;

Einumber:
    error = "inode out of bounds";

bad_entry:
    fprintf(stderr, "ufs_check_page: bad entry in directory #%llu: %s - "
            "offset=%llu, rec_len=%d, name_len=%d",
            dir->I_ino, error, (index << PAGE_CACHE_SHIFT) + offs,
            rec_len, ufs_get_de_namlen(sb, p));
    goto fail;

Eend:
    p = (struct ufs_dir_entry *)(kaddr + offs);

    fprintf(stderr, "entry in directory #%llu spans the page boundary"
           "offset=%llu", dir->I_ino, (index << PAGE_CACHE_SHIFT) + offs);
fail:
    return fres;
}

static unsigned
ufs_last_byte(struct inode* inode, unsigned long page_nr)
{
    unsigned last_byte = inode->I_size;

    last_byte -= page_nr << PAGE_CACHE_SHIFT;
    if (last_byte > PAGE_CACHE_SIZE)
        last_byte = PAGE_CACHE_SIZE;

    return last_byte;
}

static struct ufs_dir_entry*
ufs_dotdot_s(struct inode* dir, char* pagebuf)
{
    int ret = ufs_get_dirpage(dir, 0, pagebuf);
    struct ufs_dir_entry* de = NULL;
    if (ret == 0)
        de = ufs_next_entry(dir->I_sb, (struct ufs_dir_entry*)pagebuf);
    return de;
}

static int
ufs_get_dirpage(struct inode* inode, sector_t index, char* pagebuf)
{
    int ret = U_ufs_get_page(inode, index, pagebuf);
    /* if (ufs_check_page(inode, index, pagebuf) != 0) return -1; */
    return ret;
}

static ino_t
ufs_find_entry_s(struct inode* dir, const char* name)
{
    struct super_block* sb = dir->I_sb;
    struct ufs_inode_info* ui = dir->I_private;

    int namelen = strlen(name);
    unsigned reclen = UFS_DIR_REC_LEN(namelen);
    unsigned long npages = ufs_dir_pages(dir);
    unsigned long start, n;
    struct ufs_dir_entry* de;

    ino_t result = 0;

    UFSD("ENTER, dir_ino %llu, name %s, namlen %u\n",
         dir->I_ino, name, namelen);

    if (npages == 0 || namelen > UFS_MAXNAMLEN)
        goto out;

    start = ui->i_dir_start_lookup;

    if (start >= npages)
        start = 0;

    n = start;

    do {
        char page[PAGE_SIZE];
        char* kaddr;

        if (ufs_get_dirpage(dir, n, page) == 0) {
            kaddr = page;
            de = (struct ufs_dir_entry*)kaddr;
            kaddr += ufs_last_byte(dir, n) - reclen;
            while ((char*)de <= kaddr) {
                if (de->d_reclen == 0) {
                    fprintf(stderr, "zero-length directory entry\n");
                    goto out;
                }
                if (ufs_match(sb, namelen, (char*)name, de)) {
                    result = fs32_to_cpu(sb, de->d_ino);
                    goto found;
                }
                de = ufs_next_entry(sb, de);
            }
        }

        if (++n >= npages)
            n = 0;
    } while (n != start);

out:
    return result;

found:
    ui->i_dir_start_lookup = n;

    return result;
}

struct super_block*
U_ufs_fill_super(int fd, void* data, int silent)
{
    struct super_block* sb = NULL;
    struct ufs_sb_info* sbi = NULL;
    struct ufs_sb_private_info* uspi;

    struct ufs_super_block_first*  usb1;
    struct ufs_super_block_second* usb2;
    struct ufs_super_block_third*  usb3;

    struct ufs_buffer_head* ubh;    

    unsigned block_size, super_block_size;
    unsigned flags, super_block_offset;

    uspi = NULL;
    ubh = NULL;
    flags = 0;

    UFSD("ENTER\n");

    sb = calloc(1, sizeof(struct super_block));
    if (!sb)
        return NULL;

    sb->s_bdev = fd;        
    sb->s_flags |= MS_RDONLY;

    sbi = kzalloc(sizeof(struct ufs_sb_info), GFP_KERNEL);
    if (!sbi)
        goto failed_nomem;

    sbi->s_mount_opt = 0;
    sb->s_fs_info = sbi;

    ufs_set_opt(sbi->s_mount_opt, ONERROR_LOCK);
    if (!ufs_parse_options((char*) data, &sbi->s_mount_opt)) {
        printk("wrong mount options\n");
        goto failed;
    }

    if (!(sbi->s_mount_opt & UFS_MOUNT_UFSTYPE))
        ufs_set_opt(sbi->s_mount_opt, UFSTYPE_OLD);

    uspi = kzalloc(sizeof(struct ufs_sb_private_info), GFP_KERNEL);
    sbi->s_uspi = uspi;
    if (!uspi)
        goto failed;

    uspi->s_dirblksize = UFS_SECTOR_SIZE;
    super_block_offset=UFS_SBLOCK;

    switch (sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) {

    case UFS_MOUNT_UFSTYPE_44BSD:
        UFSD("ufstype=44bsd\n");
        uspi->s_fsize  = block_size = 512;
        uspi->s_fmask  = ~(512 - 1);
        uspi->s_fshift = 9;
        uspi->s_sbsize = super_block_size = 1536;
        uspi->s_sbbase = 0;
        flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
        break;

    case UFS_MOUNT_UFSTYPE_UFS2:
        UFSD("ufstype=ufs2\n");
        super_block_offset = SBLOCK_UFS2;
        uspi->s_fsize  = block_size = 512;
        uspi->s_fmask  = ~(512 - 1);
        uspi->s_fshift = 9;
        uspi->s_sbsize = super_block_size = 1536;
        uspi->s_sbbase =  0;
        flags |= UFS_TYPE_UFS2 | UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
        break;
        
    case UFS_MOUNT_UFSTYPE_SUN:
        UFSD("ufstype=sun\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        uspi->s_maxsymlinklen = 0; /* Not supported on disk */
        flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUN | UFS_CG_SUN;
        break;

    case UFS_MOUNT_UFSTYPE_SUNOS:
        UFSD(("ufstype=sunos\n"))
        uspi->s_fsize    = block_size = 1024;
        uspi->s_fmask    = ~(1024 - 1);
        uspi->s_fshift   = 10;
        uspi->s_sbsize   = 2048;
        super_block_size = 2048;
        uspi->s_sbbase   = 0;
        uspi->s_maxsymlinklen = 0; /* Not supported on disk */
        flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_SUNOS | UFS_CG_SUN;
        break;

    case UFS_MOUNT_UFSTYPE_SUNx86:
        UFSD("ufstype=sunx86\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        uspi->s_maxsymlinklen = 0; /* Not supported on disk */
        flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUNx86 | UFS_CG_SUN;
        break;

    case UFS_MOUNT_UFSTYPE_OLD:
        UFSD("ufstype=old\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
        if (!(sb->s_flags & MS_RDONLY)) {
            if (!silent)
                printk(KERN_INFO "ufstype=old is supported read-only\n");
            sb->s_flags |= MS_RDONLY;
        }
        break;
    
    case UFS_MOUNT_UFSTYPE_NEXTSTEP:
        UFSD("ufstype=nextstep\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        uspi->s_dirblksize = 1024;
        flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
        if (!(sb->s_flags & MS_RDONLY)) {
            if (!silent)
                printk(KERN_INFO "ufstype=nextstep is supported read-only\n");
            sb->s_flags |= MS_RDONLY;
        }
        break;
    
    case UFS_MOUNT_UFSTYPE_NEXTSTEP_CD:
        UFSD("ufstype=nextstep-cd\n");
        uspi->s_fsize  = block_size = 2048;
        uspi->s_fmask  = ~(2048 - 1);
        uspi->s_fshift = 11;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        uspi->s_dirblksize = 1024;
        flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
        if (!(sb->s_flags & MS_RDONLY)) {
            if (!silent)
                printk(KERN_INFO "ufstype=nextstep-cd is supported read-only\n");
            sb->s_flags |= MS_RDONLY;
        }
        break;
    
    case UFS_MOUNT_UFSTYPE_OPENSTEP:
        UFSD("ufstype=openstep\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        uspi->s_dirblksize = 1024;
        flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
        if (!(sb->s_flags & MS_RDONLY)) {
            if (!silent)
                printk(KERN_INFO "ufstype=openstep is supported read-only\n");
            sb->s_flags |= MS_RDONLY;
        }
        break;
    
    case UFS_MOUNT_UFSTYPE_HP:
        UFSD("ufstype=hp\n");
        uspi->s_fsize  = block_size = 1024;
        uspi->s_fmask  = ~(1024 - 1);
        uspi->s_fshift = 10;
        uspi->s_sbsize = super_block_size = 2048;
        uspi->s_sbbase = 0;
        flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
        if (!(sb->s_flags & MS_RDONLY)) {
            if (!silent)
                printk(KERN_INFO "ufstype=hp is supported read-only\n");
            sb->s_flags |= MS_RDONLY;
         }
         break;

    default:
        if (!silent)
            printk("unknown ufstype\n");
        goto failed;
    }
    
again:    

    /* set block size */

    if (block_size > PAGE_SIZE || block_size < 512 ||
        !is_power_of_2(block_size))
        goto failed;

    if (block_size < 512)
        goto failed;

    sb->s_blocksize = block_size;
    sb->s_blocksize_bits = blksize_bits(block_size);

    /* Read UFS super block from device */

    ubh = ubh_bread_uspi(uspi, sb,
              uspi->s_sbbase + super_block_offset/block_size, super_block_size);
    
    if (!ubh) 
        goto failed;

    usb1 = ubh_get_usb_first(uspi);
    usb2 = ubh_get_usb_second(uspi);
    usb3 = ubh_get_usb_third(uspi);

    /* Sort out mod used on SunOS 4.1.3 for fs_state */

    uspi->s_postblformat = fs32_to_cpu(sb, usb3->fs_postblformat);
    if (((flags & UFS_ST_MASK) == UFS_ST_SUNOS) &&
        (uspi->s_postblformat != UFS_42POSTBLFMT)) {
        flags &= ~UFS_ST_MASK;
        flags |=  UFS_ST_SUN;
    }

    /* Check UFS magic number */

    sbi->s_bytesex = BYTESEX_LE;

    switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
    case UFS_MAGIC:
    case UFS2_MAGIC:
    case UFS_MAGIC_LFN:
    case UFS_MAGIC_FEA:
    case UFS_MAGIC_4GB:
        goto magic_found;
    }

    sbi->s_bytesex = BYTESEX_BE;

    switch ((uspi->fs_magic = fs32_to_cpu(sb, usb3->fs_magic))) {
    case UFS_MAGIC:
    case UFS2_MAGIC:
    case UFS_MAGIC_LFN:
    case UFS_MAGIC_FEA:
    case UFS_MAGIC_4GB:
        goto magic_found;
    }

    if ((((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) ==
           UFS_MOUNT_UFSTYPE_NEXTSTEP)    ||
         ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) ==
           UFS_MOUNT_UFSTYPE_NEXTSTEP_CD) ||
         ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) ==
           UFS_MOUNT_UFSTYPE_OPENSTEP)) && uspi->s_sbbase < 256) {
        ubh_brelse_uspi(uspi);
        ubh = NULL;
        uspi->s_sbbase += 8;
        goto again;
    }

    if (!silent)
        printk("ufs_read_super: bad magic number\n");

    goto failed;

magic_found:

    /* Check block and fragment sizes */

    uspi->s_bsize  = fs32_to_cpu(sb, usb1->fs_bsize);
    uspi->s_fsize  = fs32_to_cpu(sb, usb1->fs_fsize);
    uspi->s_sbsize = fs32_to_cpu(sb, usb1->fs_sbsize);
    uspi->s_fmask  = fs32_to_cpu(sb, usb1->fs_fmask);
    uspi->s_fshift = fs32_to_cpu(sb, usb1->fs_fshift);

    if (!is_power_of_2(uspi->s_fsize)) {
        printk(KERN_ERR
               "ufs_read_super: fragment size %u is not a power of 2\n",
        uspi->s_fsize);
        goto failed;
    }

    if (uspi->s_fsize < 512) {
        printk(KERN_ERR "ufs_read_super: fragment size %u is too small\n",
               uspi->s_fsize);
        goto failed;
    }

    if (uspi->s_fsize > 4096) {
        printk(KERN_ERR "ufs_read_super: fragment size %u is too large\n",
               uspi->s_fsize);
        goto failed;
    }

    if (!is_power_of_2(uspi->s_bsize)) {
        printk(KERN_ERR "ufs_read_super: block size %u is not a power of 2\n",
               uspi->s_bsize);
        goto failed;
    }

    if (uspi->s_bsize < 4096) {
        printk(KERN_ERR "ufs_read_super: block size %u is too small\n",
               uspi->s_bsize);
        goto failed;
    }

    if (uspi->s_bsize / uspi->s_fsize > 8) {
        printk(KERN_ERR "ufs_read_super: too many fragments per block (%u)\n",
               uspi->s_bsize / uspi->s_fsize);
        goto failed;
    }

    if (uspi->s_fsize != block_size || uspi->s_sbsize != super_block_size) {
        ubh_brelse_uspi(uspi);
        ubh = NULL;
        block_size = uspi->s_fsize;
        super_block_size = uspi->s_sbsize;
        UFSD("another value of block_size or super_block_size %u, %u\n",
             block_size, super_block_size);
        goto again;
    }

    sbi->s_flags = flags; /* After this line some functions use s_flags*/

    /* ufs_print_super_stuff(sb, usb1, usb2, usb3); */

    /* Check if file system was unmounted cleanly. Make read-only otherwise. */

    if (((flags & UFS_ST_MASK) == UFS_ST_44BSD) ||
        ((flags & UFS_ST_MASK) == UFS_ST_OLD)   ||
        (((flags & UFS_ST_MASK) == UFS_ST_SUN   ||
        (flags & UFS_ST_MASK) == UFS_ST_SUNOS   ||
        (flags & UFS_ST_MASK) == UFS_ST_SUNx86) &&
        (ufs_get_fs_state(sb, usb1, usb3) ==
         (UFS_FSOK - fs32_to_cpu(sb, usb1->fs_time))))) {

        switch(usb1->fs_clean) {

        case UFS_FSCLEAN:
            UFSD("fs is clean\n");
            break;

        case UFS_FSSTABLE:
            UFSD("fs is stable\n");
            break;

        case UFS_FSOSF1:
            UFSD("fs is DEC OSF/1\n");
            break;

        case UFS_FSACTIVE:
            printk("ufs_read_super: fs is active\n");
            sb->s_flags |= MS_RDONLY;
            break;

        case UFS_FSBAD:
            printk("ufs_read_super: fs is bad\n");
            sb->s_flags |= MS_RDONLY;
            break;

        default:
            printk("ufs_read_super: can't grok fs_clean 0x%x\n",
                   usb1->fs_clean);
            sb->s_flags |= MS_RDONLY;
            break;
        }
    } else {
        printk("ufs_read_super: fs needs fsck\n");
        sb->s_flags |= MS_RDONLY;
    }

    /* Read ufs_super_block into internal data structures */

    sb->s_magic = fs32_to_cpu(sb, usb3->fs_magic);

    uspi->s_sblkno   = fs32_to_cpu(sb, usb1->fs_sblkno);
    uspi->s_cblkno   = fs32_to_cpu(sb, usb1->fs_cblkno);
    uspi->s_iblkno   = fs32_to_cpu(sb, usb1->fs_iblkno);
    uspi->s_dblkno   = fs32_to_cpu(sb, usb1->fs_dblkno);
    uspi->s_cgoffset = fs32_to_cpu(sb, usb1->fs_cgoffset);
    uspi->s_cgmask   = fs32_to_cpu(sb, usb1->fs_cgmask);

    if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
        uspi->s_u2_size  = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_size);
        uspi->s_u2_dsize = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize);
    } else {
        uspi->s_size  = fs32_to_cpu(sb, usb1->fs_size);
        uspi->s_dsize = fs32_to_cpu(sb, usb1->fs_dsize);
    }

    uspi->s_ncg = fs32_to_cpu(sb, usb1->fs_ncg);

    /* s_bsize already set */
    /* s_fsize already set */

    uspi->s_fpb     = fs32_to_cpu(sb, usb1->fs_frag);
    uspi->s_minfree = fs32_to_cpu(sb, usb1->fs_minfree);
    uspi->s_bmask   = fs32_to_cpu(sb, usb1->fs_bmask);
    uspi->s_fmask   = fs32_to_cpu(sb, usb1->fs_fmask);
    uspi->s_bshift  = fs32_to_cpu(sb, usb1->fs_bshift);
    uspi->s_fshift  = fs32_to_cpu(sb, usb1->fs_fshift);

    UFSD("uspi->s_bshift = %d,uspi->s_fshift = %d", uspi->s_bshift,
         uspi->s_fshift);

    uspi->s_fpbshift = fs32_to_cpu(sb, usb1->fs_fragshift);
    uspi->s_fsbtodb  = fs32_to_cpu(sb, usb1->fs_fsbtodb);

    /* s_sbsize already set */

    uspi->s_csmask     = fs32_to_cpu(sb, usb1->fs_csmask);
    uspi->s_csshift    = fs32_to_cpu(sb, usb1->fs_csshift);
    uspi->s_nindir     = fs32_to_cpu(sb, usb1->fs_nindir);
    uspi->s_inopb      = fs32_to_cpu(sb, usb1->fs_inopb);
    uspi->s_nspf       = fs32_to_cpu(sb, usb1->fs_nspf);
    uspi->s_npsect     = ufs_get_fs_npsect(sb, usb1, usb3);
    uspi->s_interleave = fs32_to_cpu(sb, usb1->fs_interleave);
    uspi->s_trackskew  = fs32_to_cpu(sb, usb1->fs_trackskew);

    if (uspi->fs_magic == UFS2_MAGIC)
        uspi->s_csaddr = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_csaddr);
    else
        uspi->s_csaddr = fs32_to_cpu(sb, usb1->fs_csaddr);

    uspi->s_cssize = fs32_to_cpu(sb, usb1->fs_cssize);
    uspi->s_cgsize = fs32_to_cpu(sb, usb1->fs_cgsize);
    uspi->s_ntrak  = fs32_to_cpu(sb, usb1->fs_ntrak);
    uspi->s_nsect  = fs32_to_cpu(sb, usb1->fs_nsect);
    uspi->s_spc    = fs32_to_cpu(sb, usb1->fs_spc);
    uspi->s_ipg    = fs32_to_cpu(sb, usb1->fs_ipg);
    uspi->s_fpg    = fs32_to_cpu(sb, usb1->fs_fpg);
    uspi->s_cpc    = fs32_to_cpu(sb, usb2->fs_un.fs_u1.fs_cpc);
    uspi->s_contigsumsize = fs32_to_cpu(sb,
                                        usb3->fs_un2.fs_44.fs_contigsumsize);
    uspi->s_qbmask = ufs_get_fs_qbmask(sb, usb3);
    uspi->s_qfmask = ufs_get_fs_qfmask(sb, usb3);
    uspi->s_nrpos  = fs32_to_cpu(sb, usb3->fs_nrpos);
    uspi->s_postbloff = fs32_to_cpu(sb, usb3->fs_postbloff);
    uspi->s_rotbloff  = fs32_to_cpu(sb, usb3->fs_rotbloff);

    /* Compute another frequently used values */

    uspi->s_fpbmask = uspi->s_fpb - 1;
    if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2)
        uspi->s_apbshift = uspi->s_bshift - 3;
    else
        uspi->s_apbshift = uspi->s_bshift - 2;

    uspi->s_2apbshift = uspi->s_apbshift * 2;
    uspi->s_3apbshift = uspi->s_apbshift * 3;
    uspi->s_apb  = 1 << uspi->s_apbshift;
    uspi->s_2apb = 1 << uspi->s_2apbshift;
    uspi->s_3apb = 1 << uspi->s_3apbshift;
    uspi->s_apbmask = uspi->s_apb - 1;
    uspi->s_nspfshift = uspi->s_fshift - UFS_SECTOR_BITS;
    uspi->s_nspb = uspi->s_nspf << uspi->s_fpbshift;
    uspi->s_inopf = uspi->s_inopb >> uspi->s_fpbshift;
    uspi->s_bpf = uspi->s_fsize << 3;
    uspi->s_bpfshift = uspi->s_fshift + 3;
    uspi->s_bpfmask = uspi->s_bpf - 1;

    if ((sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_44BSD ||
        (sbi->s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_UFS2)
        uspi->s_maxsymlinklen =
            fs32_to_cpu(sb, usb3->fs_un2.fs_44.fs_maxsymlinklen);

    ufs_setup_cstotal(sb);

    /* Read cylinder group structures */

    if (!(sb->s_flags & MS_RDONLY))
        if (!ufs_read_cylinder_structures(sb))
            goto failed;

    /*
     * ufs_read_cylinder_structures(sb);
     */

    UFSD("EXIT\n");

    return sb;

failed:

    if (ubh)
        ubh_brelse_uspi(uspi);

    kfree(uspi);
    kfree(sbi);
    sb->s_fs_info = NULL;

    UFSD("EXIT (FAILED)\n");

    return NULL;

failed_nomem:

    UFSD("EXIT (NOMEM)\n");

    return NULL;
}

int
U_ufs_statvfs(struct super_block* sb, struct statvfs* buf)
{
    struct ufs_sb_private_info *uspi= UFS_SB(sb)->s_uspi;
    unsigned flags = UFS_SB(sb)->s_flags;

    struct ufs_super_block_first*  usb1;
    struct ufs_super_block_second* usb2;
    struct ufs_super_block_third*  usb3;

    lock_kernel();

    usb1 = ubh_get_usb_first(uspi);
    usb2 = ubh_get_usb_second(uspi);
    usb3 = ubh_get_usb_third(uspi);
    
    if ((flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
        /* buf->f_type = UFS2_MAGIC; */
        buf->f_blocks = fs64_to_cpu(sb, usb3->fs_un1.fs_u2.fs_dsize);
    } else {
        /* buf->f_type = UFS_MAGIC; */
        buf->f_blocks = uspi->s_dsize;
    }

    buf->f_bfree = ufs_blkstofrags(uspi->cs_total.cs_nbfree) +
                                   uspi->cs_total.cs_nffree;
    buf->f_ffree = uspi->cs_total.cs_nifree;

    buf->f_bavail =
        (buf->f_bfree > (((long)buf->f_blocks / 100) * uspi->s_minfree))
        ? (buf->f_bfree - (((long)buf->f_blocks / 100) * uspi->s_minfree)) : 0;

    buf->f_files = uspi->s_ncg * uspi->s_ipg;
    buf->f_namemax = UFS_MAXNAMLEN;

    buf->f_frsize = sb->s_blocksize;
    buf->f_bsize = fs32_to_cpu(sb, usb1->fs_bsize);

    unlock_kernel();

    return 0;
}

int
U_ufs_iget(struct super_block* sb, struct inode* inode)
{
    struct ufs_inode_info* ufsi;
    struct ufs_sb_private_info* uspi = UFS_SB(sb)->s_uspi;
    int err;

    unsigned long ino = inode->I_ino;

    UFSD("ENTER, ino %lu\n", ino);

    if (ino < UFS_ROOTINO || ino > (uspi->s_ncg * uspi->s_ipg)) {
        fprintf(stderr, "ufs_read_inode: bad inode number (%lu)\n", ino);
        return EIO;
    }

    inode->I_sb = sb;
    inode->I_blkbits = sb->s_blocksize_bits;
    inode->I_nlink = 1;

    ufsi = inode->I_private;
    ufsi->i_unused1 = 0;
    ufsi->i_dir_start_lookup = 0;

    struct buffer_head _bh;
    struct buffer_head* bh = &_bh;

    bh->b_flags.dynamic = 0;
    if (sb_bread_intobh(sb, uspi->s_sbbase +
                        ufs_inotofsba(inode->I_ino), bh) != 0) {
        fprintf(stderr,
                "ufs_read_inode: unable to read inode %llu\n", inode->I_ino);
        goto bad_inode;
    }

    if ((UFS_SB(sb)->s_flags & UFS_TYPE_MASK) == UFS_TYPE_UFS2) {
        struct ufs2_inode* ufs2_inode = (struct ufs2_inode*)bh->b_data;
        err = ufs2_read_inode(inode, ufs2_inode + ufs_inotofsbo(inode->I_ino));
    } else {
        struct ufs_inode* ufs_inode = (struct ufs_inode*)bh->b_data;
        err = ufs1_read_inode(inode, ufs_inode + ufs_inotofsbo(inode->I_ino));
    }

    if (err)
        goto bad_inode;

    inode->I_version++;

    ufsi->i_lastfrag = (inode->I_size + uspi->s_fsize - 1) >> uspi->s_fshift;
    ufsi->i_osync = 0;

    brelse(bh);

    UFSD("EXIT\n");

    return 0;

bad_inode:

    return -1;
}

ino_t
U_ufs_inode_by_name(struct inode* dir, const char* name)
{
    return ufs_find_entry_s(dir, name);
}

int
U_ufs_next_direntry(struct inode* dir, struct unixfs_dirbuf* dirpagebuf,
                    off_t* offset, struct unixfs_direntry* dent)
{
    struct super_block* sb = dir->I_sb;
    struct ufs_inode_info* ui = dir->I_private;

    unsigned long npages = ufs_dir_pages(dir);
    unsigned long start, n;
    struct ufs_dir_entry* de;

    UFSD("ENTER, dir_ino %llu\n", dir->I_ino);

    if (*offset > (dir->I_size - UFS_DIR_REC_LEN(1)))
        return -1;

    if (npages == 0)
        return -1;

    start = ui->i_dir_start_lookup;
    n = start;

    if (start > npages)
        return -1;

    if ((ui->i_unused1 == 0) || (*offset & ((PAGE_SIZE - 1))) == 0) {
        int ret = ufs_get_dirpage(dir, n, dirpagebuf->data);
        if (ret != 0)
            return ret;
        ui->i_dir_start_lookup++;
        ui->i_unused1 = ui->i_dir_start_lookup;
    }

    de = (struct ufs_dir_entry*)((char*)dirpagebuf->data +
                                 (*offset & (PAGE_SIZE - 1)));

    dent->ino = fs32_to_cpu(sb, de->d_ino);
    size_t nl = ufs_get_de_namlen(sb, de);;
    memcpy(dent->name, de->d_name, nl);
    dent->name[nl] = '\0';

    *offset += fs16_to_cpu(sb, de->d_reclen);

    return 0;
}

/* Interface between UFS and read/write page. */
int
U_ufs_get_block(struct inode* inode, sector_t fragment, off_t* result)
{
      int ret;
      struct buffer_head bh;

      if ((ret = ufs_getfrag_block(inode, fragment, &bh, 0)) == 0)
          *result = bh.b_blocknr;
      else
          *result = 0;

      return ret;
}

int
U_ufs_get_page(struct inode* inode, sector_t index, char* pagebuf)
{
    sector_t iblock, lblock;
    unsigned int blocksize;
    int nr, i;

    blocksize = 1 << inode->I_blkbits;

    iblock = index << (PAGE_CACHE_SHIFT - inode->I_blkbits);
    lblock = (inode->I_size + blocksize - 1) >> inode->I_blkbits;

    nr = 0;
    i = 0;

    int bytes = 0, err = 0;
    struct super_block* sb = inode->I_sb;
    struct buffer_head _bh;
    char* p = pagebuf;

    do {
        u64 phys64 = ufs_frag_map(inode, iblock, &err);
        if (phys64) {
            struct buffer_head* bh = &_bh;
            bh->b_flags.dynamic = 0;
            if (sb_bread_intobh(sb, phys64, bh) == 0) {
                memcpy(p, bh->b_data, blocksize);
                p += blocksize;
                bytes += blocksize;
                brelse(bh); 
            } else {
                err = EIO;
                fprintf(stderr, "*** fatal error: I/O error reading page\n");
                abort();
                exit(10);
            }
        } else { /* zero fill */
            memset(p, 0, blocksize);
            p += blocksize;
            bytes += blocksize;
        }

        if ((bytes >= PAGE_SIZE) || (iblock >= lblock))
            break;

         iblock++;

    } while (1);

    if (err)
        return -1;

    /* check page? */

    return 0;
}
