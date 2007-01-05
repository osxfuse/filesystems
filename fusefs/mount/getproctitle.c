/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/sysctl.h>

char *
getproctitle(pid_t pid, char **title, int *len)
{
    size_t size;
    int    mib[3];
    int    argmax, target_argc;
    char  *target_argv;
    char  *cp;

    mib[0] = CTL_KERN;
    mib[1] = KERN_ARGMAX;

    size = sizeof(argmax);
    if (sysctl(mib, 2, &argmax, &size, NULL, 0) == -1) {
        goto failed;
    }

    target_argv = (char *)malloc(argmax);
    if (target_argv == NULL) {
        goto failed;
    }

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROCARGS2;
    mib[2] = pid;

    size = (size_t)argmax;
    if (sysctl(mib, 3, target_argv, &size, NULL, 0) == -1) {
        free(target_argv);
        goto failed;
    }

    memcpy(&target_argc, target_argv, sizeof(target_argc));
    cp = target_argv + sizeof(target_argc);

    for (; cp < &target_argv[size]; cp++) {
        if (*cp == '\0') {
            break;
        }
    }

    if (cp == &target_argv[size]) {
        free(target_argv);
        goto failed;
    }

    for (; cp < &target_argv[size]; cp++) {
        if (*cp != '\0') {
            break;
        }
    }

    if (cp == &target_argv[size]) {
        free(target_argv);
        goto failed;
    }

    *len = asprintf(title, "%s", basename(cp));

    free(target_argv);
    goto out;

failed:
    *title = NULL;
    *len = 0;

out:
    return *title;
}
