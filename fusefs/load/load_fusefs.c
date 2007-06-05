/*
 * Copyright (c) 2000-2004 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <grp.h>
#include <string.h>

#include <fuse_param.h>
#include <fuse_version.h>

/* Local Definitions */
#define LOAD_COMMAND        "/sbin/kextload"
#define UNLOAD_COMMAND      "/sbin/kextunload"
#define MACFUSE_MODULE_PATH "/Library/Extensions/fusefs.kext"

int
main(__unused int argc, __unused const char *argv[])
{
    int    pid = -1;
    int    result = -1;
    union  wait status;
    char   version[MAXHOSTNAMELEN + 1] = { 0 };
    size_t version_len = MAXHOSTNAMELEN;
    size_t version_len_desired = 0;
    struct vfsconf vfc = { 0 };

    result = getvfsbyname(MACFUSE_FS_TYPE, &vfc);
    if (result) { /* MacFUSE is not already loaded */
        result = -1;
        goto need_loading;
    }

    /* some version of MacFUSE is already loaded; let us check it out */

    result = sysctlbyname(SYSCTL_MACFUSE_VERSION_NUMBER, version,
                          &version_len, NULL, NULL);
    if (result) {
        if (errno == ENOENT) {
            /* too old; doesn't even have the sysctl variable */
            goto need_unloading;
        }
        result = -1;
        goto out;
    }

    /* sysctlbyname() includes the trailing '\0' in version_len */
    version_len_desired = strlen(MACFUSE_VERSION) + 1;

    if ((version_len == version_len_desired) &&
        !strncmp(MACFUSE_VERSION, version, version_len)) {
        /* what's currently loaded is good */
        result = 0;
        goto out;
    }

    /* mismatched version; need to unload what's loaded */

need_unloading:
    pid = fork();
    if (pid == 0) {
        result = execl(UNLOAD_COMMAND, UNLOAD_COMMAND, "-b",
                       MACFUSE_BUNDLE_IDENTIFIER, NULL);
        /* We can only get here if the exec failed */
        goto out;
    }

    if (pid == -1) {
        result = errno;
        goto out;
    }

    /* Success! */
    if ((wait4(pid, (int *)&status, 0, NULL) == pid) && (WIFEXITED(status))) {
        result = status.w_retcode;
    } else {
        result = -1;
    }

    if (result != 0) {
        /* unloading failed */
        result = -EBUSY;
        goto out;
    }

    /* unloading succeeded; now load the on-disk version */

need_loading:
    pid = fork();
    if (pid == 0) {
        result = execl(LOAD_COMMAND, LOAD_COMMAND, MACFUSE_MODULE_PATH, NULL);
        /* We can only get here if the exec failed */
        goto out;
    }
    
    if (pid == -1) {
        result = errno;
        goto out;
    }
    
    /* Success! */
    if ((wait4(pid, (int *)&status, 0, NULL) == pid) && (WIFEXITED(status))) {
        result = status.w_retcode;
    } else {
        result = -1;
    }

    /* now do any kext-load-time settings we need to do as root */

    if (result == 0) {
        int admin_gid = 0;
        struct group *g = getgrnam(MACOSX_ADMIN_GROUP_NAME);
        if (!g) {
            goto out;
        }
        admin_gid = g->gr_gid;

        /* if this fails, we don't care */
        (void)sysctlbyname(SYSCTL_MACFUSE_TUNABLES_ADMIN, NULL, NULL,
                          &admin_gid, sizeof(admin_gid));
    }
    
out:
    _exit(result);
}
