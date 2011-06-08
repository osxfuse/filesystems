/*
 * UFS for MacFUSE
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

static const char* PROGNAME = "ufs";
static const char* PROGVERS = "1.0";

static struct filesystems {
    int   isalias;
    char* fstypename;
    char* fstypename_canonical;
} filesystems[] = {
    { 0, "old",         "ufs" },
    { 0, "sunos",       "ufs" },
    { 0, "sun",         "ufs" },
    { 0, "sunx86",      "ufs" }, 
    { 0, "hp",          "ufs" },
    { 0, "nextstep",    "ufs" },
    { 0, "nextstep-cd", "ufs" },
    { 0, "openstep",    "ufs" },
    { 0, "44bsd",       "ufs" },
    { 0, "5xbsd",       "ufs" },
    { 0, "ufs2",        "ufs" },
    { 0, NULL, NULL },
};

__private_extern__
void
unixfs_usage(void)
{
    fprintf(stderr,
    "%s (version %s): UFS family of file systems for MacFUSE\n"
    "Amit Singh <http://osxbook.com>\n"
    "usage:\n"
    "      %s [--force] --dmg DMG --type TYPE MOUNTPOINT [MacFUSE args...]\n"
    "where:\n"
    "     . DMG must point to an ancient Unix disk image of a valid type\n"
    "     . TYPE is one of:",
    PROGNAME, PROGVERS, PROGNAME);

    int i;
    for (i = 0; filesystems[i].fstypename != NULL; i++) {
        if (!filesystems[i].isalias)
            fprintf(stderr, " %s ", filesystems[i].fstypename);
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

    if (!type || !*type)
        goto out;

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
