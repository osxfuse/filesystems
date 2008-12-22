/*
 * Ancient UNIX File Systems for MacFUSE
 * Amit Singh
 * http://osxbook.com
 */

#include "unixfs.h"
#include "ancientfs.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#if __linux__
#define __USE_GNU 1
#endif
#include <dlfcn.h>

static const char* PROGNAME = "ancientfs";
static const char* PROGVERS = "1.0";

static struct filesystems {
    int      isalias;
    char*    fstypename;
    char*    fstypename_canonical;
    uint32_t flags;
    char*    description;
    uint32_t magicoffset;
    char     magicbytes[32];
    char     magiclen;
} filesystems[] = {

    {
        0, "v1tap", "tap",
        ANCIENTFS_UNIX_V1 | ANCIENTFS_GENTAPE,
        "DECtape 'tap' tape archive; UNIX V1",
        0, { 0 }, 0,
    },
    {
        0, "v2tap", "tap",
        ANCIENTFS_UNIX_V2 | ANCIENTFS_GENTAPE,
        "DECtape 'tap' tape archive; UNIX V2",
        0, { 0 }, 0,
    },
    {
        0, "v3tap", "tap",
        ANCIENTFS_UNIX_V3 | ANCIENTFS_GENTAPE,
        "DECtape 'tap' tape archive; UNIX V3",
        0, { 0 }, 0,
    },
    {
        0, "ntap", "tap",
        ANCIENTFS_GENTAPE,
        "DECtape/magtape 'tap' tape archive; 1970 epoch",
        0, { 0 }, 0,
    },
    {
        0, "tp", "tp",
        ANCIENTFS_GENTAPE,
        "DECtape/magtape 'tp' tape archive",
        0, { 0 }, 0,
    },
    {
        0, "itp", "itp",
        ANCIENTFS_GENTAPE,
        "UNIX 'itp' tape archive",
        0, { 0 }, 0,
    },
    {   0, "dtp", "dtp",
        ANCIENTFS_GENTAPE,
        "UNIX 'dtp' tape archive",
        0, { 0 }, 0,
    },
    {   0, "dump", "dump",
        0,
        "Incremental file system dump (512-byte blocks, V7/bsd)",
        0, { 0 }, 0,
    },
    {   1, "dump512" , "dump",
        0,
        "Incremental file system dump (512-byte blocks, V7/bsd)",
        0, { 0 }, 0,
    },
    {   0, "dump1k", "dump1024",
        ANCIENTFS_DUMP1KB,
        "Incremental file system dump (1024-byte blocks, V7/bsd)",
        0, { 0 }, 0,
    },
    {   0, "dump-vn", "dumpvn",
        0,
        "Incremental file system dump (512-byte blocks, bsd-vn)",
        0, { 0 }, 0,
    },
    {   0, "dump1k-vn", "dumpvn1024",
        ANCIENTFS_DUMP1KB,
        "Incremental file system dump (1024-byte blocks, bsd-vn)",
        0, { 0 }, 0,
    },
    {
        0, "v1ar", "voar",
        ANCIENTFS_UNIX_V1,
        "Very old (0177555) archive (.a) from First Edition UNIX",
        0, { 0x6d, 0xff }, 2,
    },
    {
        0, "v2ar", "voar",
        ANCIENTFS_UNIX_V2,
        "Very old (0177555) archive (.a) from Second Edition UNIX",
        0, { 0x6d, 0xff }, 2,
    },
    {
        0, "v3ar", "voar",
        ANCIENTFS_UNIX_V3,
        "Very old (0177555) archive (.a) from Third Edition UNIX",
        0, { 0x6d, 0xff }, 2,
    },
    {
        1, "ar", "voar",
        0,
        "Very old (0177555) archive (.a)",
        0, { 0x6d, 0xff }, 2,
    },
    {
        1, "ar", "oar",
        0,
        "Old (0177545) archive (.a)",
        0, { 0x65, 0xff }, 2,
    },
    {
        0, "ar", "ar",
        0,
        "Current (!<arch>\\n), old (0177545), or very old (0177555)\n"
        "                     archive (.a); use (v1|v2|v3)ar for UNIX V1/V2/V3 "
        "archives",
        0, { 0x21, 0x3c, 0x61, 0x72, 0x63, 0x68, 0x3e, 0x0a }, 8,
    },
    {
        0, "bcpio", "bcpio",
        0,
        "Binary cpio archive (old); may be byte-swapped",
        0, { 0x71, 0xc7 }, 2,
    },
    {
        1, "bcpio", "bcpio",
        0,
        "Binary cpio archive (old); may be byte-swapped",
        0, { 0xc7, 0x71 }, 2,
    },
    {
        0, "cpio_odc", "cpio_odc",
        0,
        "ASCII (odc) cpio archive",
        0, { 0x30, 0x37, 0x30, 0x37, 0x30, 0x37 }, 6,
    },
    {
        0, "cpio_newc", "cpio_newc",
        0,
        "New ASCII (newc) cpio archive",
        0, { 0x30, 0x37, 0x30, 0x37, 0x30, 0x31 }, 6,
    },
    {
        0, "cpio_newcrc", "cpio_newc",
        ANCIENTFS_NEWCRC,
        "New ASCII (newc) cpio archive with checksum",
        0, { 0x30, 0x37, 0x30, 0x37, 0x30, 0x32 }, 6,
    },
    {
        0, "tar", "tar",
        0,
        "ustar, pre-POSIX ustar, or V7 tar archive",
        257, { 0x75, 0x73, 0x74, 0x61, 0x72, 0x0 }, 6, /* POSIX ustar */
    },
    {
        1, "tar", "tar",
        0,
        "ustar, pre-POSIX ustar, or V7 tar archive",
        257, { 0x75, 0x73, 0x74, 0x61, 0x72, 0x20 }, 6, /* pre-POSIX ustar */
    },
    {
        1, "tar", "tar",
        0,
        "ustar, pre-POSIX ustar, or V7 tar archive",
        0, { 0 }, 0, /* V7 tar */
    },
    {
        0, "v1", "v123",
        ANCIENTFS_UNIX_V1,
        "First Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v2", "v123",
        ANCIENTFS_UNIX_V2,
        "Second Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v3", "v123",
        ANCIENTFS_UNIX_V3,
        "Third Edition UNIX file system",
        0, { 0 }, 0,
    }, 
    {
        0, "v4", "v456",
        ANCIENTFS_UNIX_V4,
        "Fourth Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v5", "v456",
        ANCIENTFS_UNIX_V5,
        "Fifth Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v6", "v456",
        ANCIENTFS_UNIX_V6,
        "Sixth Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v7", "v7",
        0,
        "Seventh Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "v10", "v10",
        ANCIENTFS_UNIX_V10,
        "Tenth Edition UNIX file system",
        0, { 0 }, 0,
    },
    {
        0, "32v", "32v",
        0,
        "UNIX/32V file system",
        0, { 0 }, 0,
    },
    {
        1, "32/v", "32v",
        0,
        "UNIX/32V file system",
        0, { 0 }, 0,
    },
    {
        1, "2.9bsd", "29bsd",
        0,
        "BSD file system (V7-style with fixed-length file names)",
        0, { 0 }, 0,
    },
    {
        1, "29bsd", "29bsd",
        0,
        "BSD file system (V7-style with fixed-length file names;\n"
        "                   e.g. 2.9BSD or 4.0BSD)",
        0, { 0 }, 0,
    },
    {
        0, "bsd", "29bsd",
        0,
        "BSD file system (V7-style with fixed-length file names;\n"
        "                     e.g. 2.9BSD or 4.0BSD)",
        0, { 0 }, 0,
    },
    {
        1, "2.11bsd", "211bsd",
        0,
        "BSD file system (pre 'fast-file-system' \"UFS\" with\n"
        "                     variable-length file names; e.g. 2.11BSD "
        "for PDP-11)",
        0, { 0 }, 0,
    },
    {
        1, "211bsd", "211bsd",
        0,
        "BSD file system (pre 'fast-file-system' \"UFS\" with\n"
        "                     variable-length file names; e.g. 2.11BSD "
        "for PDP-11)",
        0, { 0 }, 0,
    },
    {
        0, "bsd-vn", "211bsd",
        0,
        "BSD file system (pre 'fast-file-system' \"UFS\" with\n"
        "                     variable-length file names; e.g. 2.11BSD "
        "for PDP-11)",
        0, { 0 }, 0,
    },
    /* done */
    { 0, NULL, NULL,  0 },
};

__private_extern__
void
unixfs_usage(void)
{
    fprintf(stderr,
"AncientFS (%s): a MacFUSE file system to mount ancient Unix disks and tapes\n"
"Amit Singh <http://osxbook.com>\n"
"usage:\n"
"      %s [--force] --dmg DMG --type TYPE MOUNTPOINT [MacFUSE args...]\n"
"where:\n"
"     . DMG is an ancient Unix disk or tape image of a valid type\n"
"     . TYPE is one of the following:\n\n",
PROGVERS, PROGNAME);

    int i;
    for (i = 0; filesystems[i].fstypename != NULL; i++) {
        if (!filesystems[i].isalias)
            fprintf(stderr, "         %-11s %s\n", filesystems[i].fstypename,
                    filesystems[i].description);
    }
    fprintf(stderr, "\n");

    fprintf(stderr, "%s",
    "     . --force attempts mounting even if there are warnings or errors\n"
    );
}

__private_extern__
struct unixfs*
unixfs_preflight(char* dmg, char** type, struct unixfs** unixfsp)
{
    int i;
    *unixfsp = NULL;

    if (!type)
        goto out;

    int fd;
    char buf[512];

    if (!dmg) {
        fprintf(stderr, "no image specified\n");
        goto out;
    }

    if ((fd = open(dmg, O_RDONLY)) < 0) {
        fprintf(stderr, "failed to open %s\n", dmg);
        goto out;
    }

    if (read(fd, buf, 512) != 512) {
        close(fd);
        fprintf(stderr, "failed to read data from %s\n", dmg);
    }

    close(fd);

    if (!*type) {

        /* no type; try some matching */
        for (i = 0; filesystems[i].fstypename != NULL; i++) {
            if (filesystems[i].magiclen == 0)
                continue;
            /* ok, got something; try */
            if (memcmp(filesystems[i].magicbytes,
                       (char*)&buf[filesystems[i].magicoffset],
                       filesystems[i].magiclen) == 0) {
                *type = filesystems[i].fstypename;
                goto findhandler;
            }
        }

        goto out;
    }

findhandler:

    for (i = 0; filesystems[i].fstypename != NULL; i++) {
        if (strcasecmp(*type, filesystems[i].fstypename) == 0) {
            /* If this has a non-zero magic length, must do magic too. */
            if (filesystems[i].magiclen) {
                if (memcmp(filesystems[i].magicbytes,
                           (char*)&buf[filesystems[i].magicoffset],
                           filesystems[i].magiclen) != 0) {
                    /* not this one */
                    continue;
                }
            }
            char symb[255];
            snprintf(symb, 255, "%s_%s", "unixfs",
                     filesystems[i].fstypename_canonical);
            void* impl = dlsym(RTLD_DEFAULT, symb);
            if (impl != NULL) {
                *unixfsp = (struct unixfs*)impl;
                (*unixfsp)->flags = filesystems[i].flags;
                break;
            }
        }
    }

out:
    return *unixfsp;
}

__private_extern__
void
unixfs_postflight(char* fsname, char* volname, char* extra_args)
{
    snprintf(extra_args, UNIXFS_ARGLEN,
        "-oro,defer_permissions,daemon_timeout=5,"
        "volname=%s,fsname=%s File System",
        volname, fsname);
}
