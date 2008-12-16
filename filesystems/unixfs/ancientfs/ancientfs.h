/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#ifndef _ANCIENTFS_H_
#define _ANCIENTFS_H_

/* only upper-half bits */

#define ANCIENTFS_UNIX_V1   0x80000000
#define ANCIENTFS_UNIX_V2   0x40000000
#define ANCIENTFS_UNIX_V3   0x20000000
#define ANCIENTFS_UNIX_V4   0x10000000
#define ANCIENTFS_UNIX_V5   0x08000000
#define ANCIENTFS_UNIX_V6   0x04000000
#define ANCIENTFS_UNIX_V10  0x02000000
#define ANCIENTFS_GENTAPE   0x01000000
#define ANCIENTFS_DECTAPE   0x00800000
#define ANCIENTFS_MAGTAPE   0x00400000
#define ANCIENTFS_DUMP1KB   0x00200000
#define ANCIENTFS_VERYOLDAR 0x00100000 
#define ANCIENTFS_V7TAR     0x00080000
#define ANCIENTFS_USTAR     0x00040000
#define ANCIENTFS_NEWCRC    0x00020000

#define TAPEDIR_BEGIN_BLOCK_GENERIC 1
#define TAPEDIR_END_BLOCK_GENERIC   (1024*1024)

/*
 * Block zero of the tape is not used. It is available as a boot program to
 * be used in a standalone environment. For example, it was used for DEC
 * diagnostic programs.
 *
 * Blocks 1 through 24 contain a directory of the tape. There are 192 entries
 * in the directory; 8 entries per block; 64 bytes per entry. We can think of
 * such an entry the tape's "dinode".
 */
#define TAPEDIR_BEGIN_BLOCK_DEC 1
#define TAPEDIR_END_BLOCK_DEC   24

/*
 * In the case of magtape, the directory blocks go from 1 through 62. It has
 * 496 entries.
 */
#define TAPEDIR_BEGIN_BLOCK_MAG 1
#define TAPEDIR_END_BLOCK_MAG   62

#endif /* _ANCIENTFS_H_ */

