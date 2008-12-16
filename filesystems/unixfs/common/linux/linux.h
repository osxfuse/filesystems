#ifndef _LINUX_TYPES_H_
#define _LINUX_TYPES_H_

#if defined(__APPLE__)

#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <libkern/OSByteOrder.h>

#define __force
#define __bitwise
#define __packed2__ __attribute__((packed, aligned(2)))

#define __u64 uint64_t
#define __u32 uint32_t
#define __u16 uint16_t
#define __u8  uint8_t
#define __s64 int64_t
#define __s32 int32_t
#define __s16 int16_t
#define __s8  int8_t

typedef __u64 u64;
typedef __u32 u32;
typedef __u16 u16;
typedef __u8  u8;
typedef __s64 s64;
typedef __s32 s32;
typedef __s16 s16;
typedef __s8  s8;

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;

#ifndef __fs16
#define __fs16 __u16
#endif
#ifndef __fs32
#define __fs32 __u32
#endif

typedef unsigned gfp_t;
typedef off_t    loff_t;
typedef long     pgoff_t;
typedef uint64_t sector_t;
typedef uint16_t umode_t;

/* dummy structures */

struct dentry;
struct file;
struct super_block;
struct kstatfs {
    __u64 f_type;
    __u64 f_blocks;
    __u64 f_bfree;
    __u64 f_bavail;
    __u64 f_files;
    __u64 f_ffree;
    __u32 f_bsize;
    __u32 f_namelen;
    __u32 f_frsize;
    __u32 padding;
    __u32 f_spare[6];
};

/****** grab-bag stuff and defines ******/

#define MS_RDONLY        1
#define EXPORT_SYMBOL(s) /**/

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE      (1 << BLOCK_SIZE_BITS)

/* file types */
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

#define get_seconds()    time(0)
#define CURRENT_TIME_SEC ((struct timespec) { get_seconds(), 0 })

#define simple_strtoul strtoul
#define simple_strtol  strtol
#define printk         printf

#define lock_kernel()   /**/
#define unlock_kernel() /**/
#define KERN_ERR        /**/
#define KERN_INFO       /**/

#define __GFP_DMA     ((__force gfp_t)0x01u)
#define __GFP_HIGHMEM ((__force gfp_t)0x02u)
#define __GFP_DMA32   ((__force gfp_t)0x04u)
#define __GFP_WAIT    ((__force gfp_t)0x10u)
#define __GFP_HIGH    ((__force gfp_t)0x20u)
#define __GFP_IO      ((__force gfp_t)0x40u)
#define __GFP_FS      ((__force gfp_t)0x80u)
#define GFP_KERNEL    (__GFP_WAIT | __GFP_IO | __GFP_FS)

#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({                    \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define likely(x)               __builtin_expect(!!(x), 1)
#define unlikely(x)             __builtin_expect(!!(x), 0)

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define BUILD_BUG_ON_ZERO(e)    (sizeof(char[1 - 2 * !!(e)]) - 1)
#define BUG_ON(condition)       /**/
#define MAX_ERRNO               4095
#define IS_ERR_VALUE(x)          unlikely((x) >= (unsigned long)-MAX_ERRNO)
#define __must_be_array(a)      \
    BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(typeof(a), typeof(&a[0])))
#define ARRAY_SIZE(arr)         \
    (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

static inline long PTR_ERR(const void* ptr)
{
    return (long)ptr;
}

static inline void* ERR_PTR(long error)
{
    return (void *)error;
}

static inline long IS_ERR(const void *ptr)
{
    return IS_ERR_VALUE((unsigned long)ptr);
}

#define min_t(type, x, y) ({            \
    type __min1 = (x);                  \
    type __min2 = (y);                  \
    __min1 < __min2 ? __min1: __min2; })

#define max_t(type, x, y) ({            \
    type __max1 = (x);                  \
    type __max2 = (y);                  \
    __max1 > __max2 ? __max1: __max2; })

/****** page and memory stuff ******/

#define PAGE_SIZE        4096
#define PAGE_CACHE_SIZE  PAGE_SIZE
#define PAGE_CACHE_SHIFT 12
#define PAGE_CACHE_MASK  (~(PAGE_CACHE_SIZE-1))

struct address_space {
    struct inode* host;
};

struct page {
    union {
        unsigned long private;
        struct address_space* mapping;
    };
};

static inline struct page* find_lock_page(struct address_space* mapping,
                                          pgoff_t offset)
{
    return (struct page*)0;
}

static inline void page_noop(struct page* page)
{
    /* nothing */
}

#define lock_page          page_noop
#define unlock_page        page_noop
#define put_page           page_noop
#define page_cache_release page_noop
#define PageUptodate(x)    1
#define PageError(x)       0

static inline struct page* read_mapping_page(struct address_space* mapping,
                                             pgoff_t index, void* data)
{
    return (struct page*)0;
}

static inline void* kmalloc(size_t size, gfp_t flags)
{
    return malloc(size);
}

static inline void* kzalloc(size_t size, gfp_t flags)
{
    return calloc(1, size);
}

static inline void kfree(const void* p)
{
    free((void*)p);
}

/****** buffer stuff *****/

struct block_device {
    int fd;
    unsigned bd_block_size;
};

struct buffer_head {
    sector_t b_blocknr;
    size_t   b_size;
    struct   b_flags {
        uint32_t dynamic : 1;
    } b_flags;
    unsigned char b_data[PAGE_SIZE];
};

int sb_bread_intobh(struct super_block* sb, off_t block,
                    struct buffer_head* bh);
struct buffer_head* sb_bread(struct super_block* sb, off_t block);
struct buffer_head* sb_getblk(struct super_block* sb, sector_t block);
void brelse(struct buffer_head* bh);
#define bforget brelse

static inline void buffer_noop(struct buffer_head *bh)
{
    /* nothing */
}

static inline void ll_rw_block(int rw, int nr, struct buffer_head* bhs[])
{
    /* nothing */
}

#define mark_buffer_dirty     buffer_noop
#define set_buffer_uptodate   buffer_noop
#define clear_buffer_uptodate buffer_noop
#define wait_on_buffer        buffer_noop
#define buffer_dirty(x)       0
#define set_buffer_mapped     buffer_noop

/****** bit and math stuff ******/

#define BITOP_WORD(nr)   ((nr) / BITS_PER_LONG)
#define BITOP_LE_SWIZZLE ((BITS_PER_LONG-1) & ~0x7)

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1)
#define BITOP_ADDR(x) "=m" (*(volatile long *) (x))
#else
#define BITOP_ADDR(x) "+m" (*(volatile long *) (x))
#endif
#define ADDR  BITOP_ADDR(addr)

#if defined(__i386__) || defined(__x86_64__)
static inline void __set_bit(int nr, volatile unsigned long *addr)
{
    asm volatile("bts %1,%0" : ADDR : "Ir" (nr) : "memory");
}
#elif defined(__ppc__) || defined(__ppc64__)
static inline void __set_bit(int nr, volatile unsigned long *addr)
{
    *addr |= (1L << nr);
}
#endif

#if defined(__LITTLE_ENDIAN__)

#define generic_test_le_bit(nr, addr)   test_bit(nr, addr)
#define generic___set_le_bit(nr, addr)   __set_bit(nr, addr)
#define generic___clear_le_bit(nr, addr) __clear_bit(nr, addr)

#define generic_test_and_set_le_bit(nr, addr)   test_and_set_bit(nr, addr)
#define generic_test_and_clear_le_bit(nr, addr) test_and_clear_bit(nr, addr)

#define generic___test_and_set_le_bit(nr, addr)   __test_and_set_bit(nr, addr)
#define generic___test_and_clear_le_bit(nr, addr) __test_and_clear_bit(nr, addr)

#define generic_find_next_zero_le_bit(addr, size, offset) \
            find_next_zero_bit(addr, size, offset)
#define generic_find_next_le_bit(addr, size, offset)      \
            find_next_bit(addr, size, offset)

#elif defined(__BIG_ENDIAN__)

#define generic_test_le_bit(nr, addr)             \
    test_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define generic___set_le_bit(nr, addr)            \
    __set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define generic___clear_le_bit(nr, addr)          \
    __clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define generic_test_and_set_le_bit(nr, addr)     \
    test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define generic_test_and_clear_le_bit(nr, addr)   \
    test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define generic___test_and_set_le_bit(nr, addr)   \
    __test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define generic___test_and_clear_le_bit(nr, addr) \
    __test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

extern unsigned long generic_find_next_zero_le_bit(
                         const unsigned long *addr, unsigned long size,
                         unsigned long offset);
extern unsigned long generic_find_next_le_bit(const unsigned long *addr,
                         unsigned long size, unsigned long offset);

#endif

#define ext2_set_bit(nr,addr)   \
    generic___test_and_set_le_bit((nr),(unsigned long *)(addr))
#define ext2_clear_bit(nr,addr) \
    generic___test_and_clear_le_bit((nr),(unsigned long *)(addr))

#define ext2_test_bit(nr,addr)  \
    generic_test_le_bit((nr),(unsigned long *)(addr))
#define ext2_find_first_zero_bit(addr, size) \
    generic_find_first_zero_le_bit((unsigned long *)(addr), (size))
#define ext2_find_next_zero_bit(addr, size, off) \
    generic_find_next_zero_le_bit((unsigned long *)(addr), (size), (off))
#define ext2_find_next_bit(addr, size, off) \
    generic_find_next_le_bit((unsigned long *)(addr), (size), (off))

#if defined(__i386__) || defined(__ppc__)
#define BITS_PER_LONG 32
#elif defined(__x86_64__) || defined(__ppc64__)
#define BITS_PER_LONG 64
#endif

static inline unsigned int blksize_bits(unsigned int size)
{
    unsigned int bits = 8;
    do {
        bits++;
        size >>= 1;
    } while (size > 256);
    return bits;
}

static inline __attribute__((const)) int is_power_of_2(unsigned long n)
{
    return (n != 0 && ((n & (n - 1)) == 0));
}

static inline unsigned long ffz(unsigned long word) /* find first zero bit */
{
    asm("bsf %1,%0"
        : "=r" (word)
        : "r" (~word));
    return word;
}

static inline unsigned long find_next_zero_bit(const unsigned long *addr,
                                unsigned long size, unsigned long offset)
{
    const unsigned long* p = addr + BITOP_WORD(offset);
    unsigned long result = offset & ~(BITS_PER_LONG-1);
    unsigned long tmp;

    if (offset >= size)
        return size;

    size -= result;
    offset %= BITS_PER_LONG;

    if (offset) {
        tmp = *(p++);
        tmp |= ~0UL >> (BITS_PER_LONG - offset);
        if (size < BITS_PER_LONG)
            goto found_first;
        if (~tmp)
            goto found_middle;
        size -= BITS_PER_LONG;
        result += BITS_PER_LONG;
    }

    while (size & ~(BITS_PER_LONG-1)) {
        if (~(tmp = *(p++)))
            goto found_middle;
        result += BITS_PER_LONG;
        size -= BITS_PER_LONG;
    }

    if (!size)
        return result;

    tmp = *p;

found_first:

    tmp |= ~0UL << size;
    if (tmp == ~0UL)          /* Are any bits zero? */
        return result + size; /* Nope. */

found_middle:

    return result + ffz(tmp);
}

#define do_div(n, base)                     \
({                                  \
    unsigned long __upper, __low, __high, __mod, __base;    \
    __base = (base);                    \
    asm("":"=a" (__low), "=d" (__high) : "A" (n));      \
    __upper = __high;                       \
    if (__high) {                       \
        __upper = __high % (__base);            \
        __high = __high / (__base);             \
    }                               \
    asm("divl %2":"=a" (__low), "=d" (__mod)        \
        : "rm" (__base), "0" (__low), "1" (__upper));       \
    asm("":"=A" (n) : "a" (__low), "d" (__high));       \
    __mod;                          \
})

/****** endian stuff ******/

#define le64_to_cpu(x) OSSwapLittleToHostInt64(x)
#define be64_to_cpu(x) OSSwapBigToHostInt64(x)
#define cpu_to_le64(x) OSSwapHostToLittleInt64(x)
#define cpu_to_be64(x) OSSwapHostToBigInt64(x)

#define le32_to_cpu(x) OSSwapLittleToHostInt32(x)
#define be32_to_cpu(x) OSSwapBigToHostInt32(x)
#define cpu_to_le32(x) OSSwapHostToLittleInt32(x)
#define cpu_to_be32(x) OSSwapHostToBigInt32(x)

#define le16_to_cpu(x) OSSwapLittleToHostInt16(x)
#define be16_to_cpu(x) OSSwapBigToHostInt16(x)
#define cpu_to_le16(x) OSSwapHostToLittleInt16(x)
#define cpu_to_be16(x) OSSwapHostToBigInt16(x)

static inline void le16_add_cpu(__le16* var, u16 val)
{
    *var = cpu_to_le16(le16_to_cpu(*var) + val);
}

static inline void le32_add_cpu(__le32* var, u32 val)
{
    *var = cpu_to_le32(le32_to_cpu(*var) + val);
}

static inline void le64_add_cpu(__le64* var, u64 val)
{
    *var = cpu_to_le64(le64_to_cpu(*var) + val);
}

static inline void be16_add_cpu(__be16* var, u16 val)
{
    *var = cpu_to_be16(be16_to_cpu(*var) + val);
}

static inline void be32_add_cpu(__be32* var, u32 val)
{
    *var = cpu_to_be32(be32_to_cpu(*var) + val);
}

static inline void be64_add_cpu(__be64* var, u64 val)
{
    *var = cpu_to_be64(be64_to_cpu(*var) + val);
}

/****** device stuff ******/

#define MINORBITS    20
#define MINORMASK    ((1U << MINORBITS) - 1)
#define MAJOR(dev)   ((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)   ((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma,mi) (((ma) << MINORBITS) | (mi))

static inline int old_valid_dev(dev_t dev)
{
    return MAJOR(dev) < 256 && MINOR(dev) < 256;
}

static inline u16 old_encode_dev(dev_t dev)
{
    return (MAJOR(dev) << 8) | MINOR(dev);
}

static inline dev_t old_decode_dev(u16 val)
{
    return MKDEV((val >> 8) & 255, val & 255);
}

static inline int new_valid_dev(dev_t dev)
{
    return 1;
}

static inline u32 new_encode_dev(dev_t dev)
{
    unsigned major = MAJOR(dev);
    unsigned minor = MINOR(dev);
    return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

static inline dev_t new_decode_dev(u32 dev)
{
    unsigned major = (dev & 0xfff00) >> 8;
    unsigned minor = (dev & 0xff) | ((dev >> 12) & 0xfff00);
    return MKDEV(major, minor);
}

static inline int huge_valid_dev(dev_t dev)
{
    return 1;
}

static inline u64 huge_encode_dev(dev_t dev)
{
    return new_encode_dev(dev);
}

static inline dev_t huge_decode_dev(u64 dev)
{
    return new_decode_dev(dev);
}

static inline int sysv_valid_dev(dev_t dev)
{
    return MAJOR(dev) < (1<<14) && MINOR(dev) < (1<<18);
}

static inline u32 sysv_encode_dev(dev_t dev)
{
    return MINOR(dev) | (MAJOR(dev) << 18);
}

static inline unsigned sysv_major(u32 dev)
{
    return (dev >> 18) & 0x3fff;
}

static inline unsigned sysv_minor(u32 dev)
{
    return dev & 0x3ffff;
}

#endif /* __APPLE__ */

#endif /* _LINUX_TYPES_H_ */
