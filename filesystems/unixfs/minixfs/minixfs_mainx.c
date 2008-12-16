/*
 * Minix File System Famiy for MacFUSE
 * Copyright (c) 2008 Amit Singh
 * http://osxbook.com
 */

#include "unixfs.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dlfcn.h>

static const char* PROGNAME = "minixfs";
static const char* PROGVERS = "1.0";

static struct filesystems {
    int   isalias;
    char* fstypename;
    char* fstypename_canonical;
} filesystems[] = {
    { 1, "Minix", "minix" },
    { 0, NULL, NULL },
};

__private_extern__
void
unixfs_usage(void)
{
    fprintf(stderr,
    "%s (version %s): Minix File System for MacFUSE\n"
    "Amit Singh <http://osxbook.com>\n"
    "usage:\n"
    "      %s [--force] --dmg DMG MOUNTPOINT [MacFUSE args...]\n"
    "where:\n"
    "     . DMG must point to a Minix disk image\n"
    "     . --force attempts mounting even if there are warnings or errors\n",
    PROGNAME, PROGVERS, PROGNAME);
}

__private_extern__
struct unixfs*
unixfs_preflight(char* dmg, char** type, struct unixfs** unixfsp)
{
    int i;
    *unixfsp = NULL;

    if (!type)
        goto out;

    *type = "minix"; /* quick fix */

    for (i = 0; filesystems[i].fstypename != NULL; i++) {
        if (strcasecmp(*type, filesystems[i].fstypename) == 0) {
            char symb[255];
            snprintf(symb, 255, "%s_%s", "unixfs",
                     filesystems[i].fstypename_canonical);
            void* impl = dlsym(RTLD_DEFAULT, symb);
            if (impl != NULL) {
                *unixfsp = (struct unixfs*)impl;
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
        "-oro,sparse,defer_permissions,daemon_timeout=5,"
        "volname=%s,fsname=%s File System",
        volname, fsname);
}
