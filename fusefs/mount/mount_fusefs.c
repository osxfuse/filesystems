/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#include <err.h>
#include <libgen.h>
#include <sysexits.h>
#include <paths.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <libgen.h>
#include <signal.h>

#include "mntopts.h"
#include <fuse_ioctl.h>
#include <fuse_mount.h>
#include <fuse_version.h>

#include <CoreFoundation/CoreFoundation.h>

#define PROGNAME "mount_fusefs"
#define FUSE_INIT_WAIT_INTERVAL 100000 /* us */

char *getproctitle(pid_t pid, char **title, int *len);
void  showhelp(void);
void  showversion(int doexit);

struct mntopt mopts[] = {
    MOPT_STDOPTS,
    MOPT_UPDATE,
    { "allow_other",         0, FUSE_MOPT_ALLOW_OTHER,           1 },
    { "allow_root",          0, FUSE_MOPT_ALLOW_ROOT,            1 },
    { "blocksize=",          0, FUSE_MOPT_BLOCKSIZE,             1 }, // used
    { "debug",               0, FUSE_MOPT_DEBUG,                 1 }, // used
    { "default_permissions", 0, FUSE_MOPT_DEFAULT_PERMISSIONS,   1 },
    { "directio",            0, FUSE_MOPT_DIRECT_IO,             1 },
    { "fd=",                 0, FUSE_MOPT_FD,                    1 },
    { "fsid=" ,              0, FUSE_MOPT_FSID,                  1 }, // used
    { "fsname=",             0, FUSE_MOPT_FSNAME,                1 }, // used
    { "gid=",                0, FUSE_MOPT_GID,                   1 },
    { "hard_remove",         0, FUSE_MOPT_HARD_REMOVE,           1 },
    { "iosize=",             0, FUSE_MOPT_IOSIZE,                1 }, // used
    { "jail_symlinks",       0, FUSE_MOPT_JAIL_SYMLINKS,         1 }, // used
    { "kernel_cache",        0, FUSE_MOPT_KERNEL_CACHE,          1 },
    { "large_read",          0, FUSE_MOPT_LARGE_READ,            1 },
    { "max_read=",           0, FUSE_MOPT_MAX_READ,              1 },
    { "ping_diskarb",        0, FUSE_MOPT_PING_DISKARB,          1 }, // used

    { "attrcache",           1, FUSE_MOPT_NO_ATTRCACHE,          1 }, // used
    { "authopaque",          1, FUSE_MOPT_NO_AUTH_OPAQUE,        1 }, // used
    { "authopaqueaccess",    1, FUSE_MOPT_NO_AUTH_OPAQUE_ACCESS, 1 }, // used
    { "readahead",           1, FUSE_MOPT_NO_READAHEAD,          1 }, // used
    { "syncwrites",          1, FUSE_MOPT_NO_SYNCWRITES,         1 }, // used
    { "ubc",                 1, FUSE_MOPT_NO_UBC,                1 }, // used

    { "readdir_ino",         0, FUSE_MOPT_READDIR_INO,           1 },
    { "rootmode=",           0, FUSE_MOPT_ROOTMODE,              1 },
    { "subtype=",            0, FUSE_MOPT_SUBTYPE,               1 }, // used
    { "uid=",                0, FUSE_MOPT_UID,                   1 },
    { "umask=",              0, FUSE_MOPT_UMASK,                 1 },
    { "use_ino",             0, FUSE_MOPT_USE_INO,               1 },
    { "volname=",            0, FUSE_MOPT_VOLNAME,               1 }, // used
    { NULL }
};

typedef int (* converter_t)(void **target, void *value, void *fallback);

struct mntval {
    int         mv_mntflag;
    void       *mv_value;
    int         mv_len;
    converter_t mv_converter;
    void       *mv_fallback;
    void      **mv_target;
    char       *mv_errstr;
};

static __inline__ int
fuse_to_string(void **target, void *value, void *fallback)
{
    if (!value) {
        // think about what to do if we want to set a NULL value when the
        // fallback value is non-NULL
        value = fallback;
    }

    *target = value;

    return 0;
}

static __inline__ int
fuse_to_uint32(void **target, void *value, void *fallback)
{
    unsigned long u;

    if (!value) {
        *target = fallback;
        return 0;
    }

    errno = 0;
    u = strtoul((char *)value, NULL, 10);
    if ((errno == ERANGE) || (errno == EINVAL)) {
        *target = fallback;
        return errno;
    }

    *target = (void *)u;

    return 0;
}

static __inline__ int
fuse_to_fsid(void **target, void *value, void *fallback)
{
    int ret;
    uint32_t u;

    if (!value) {
        *target = fallback;
        return 0;
    }

    ret = fuse_to_uint32(target, value, fallback);

    if (ret) {
        return ret;
    }

    u = *(uint32_t *)target;

    if ((u & ~FUSE_MINOR_MASK) || (u == 0)) {
        return EINVAL;
    }

    return 0;
}

static __inline__ int
fuse_to_subtype(void **target, void *value, void *fallback)
{
    int ret = 0;
    uint32_t u;
    
    *(uint32_t *)target = FUSE_FSSUBTYPE_UNKNOWN;

    if (value) {
        ret = fuse_to_uint32(target, value, fallback);
        if (ret) {
            return ret;
        }   
        u = *(uint32_t *)target;
        if (u >= FUSE_FSSUBTYPE_MAX) {
            return EINVAL;
        }
        if (u > FUSE_FSSUBTYPE_UNKNOWN) {
            return 0;
        }
    }

    /* try to guess */

    *(uint32_t *)target = FUSE_FSSUBTYPE_UNKNOWN;

    {
        int   len;
        char *title;

        title = getproctitle(getppid(), &title, &len);
        if (!title) {
            return 0;
        }

        if (strcasestr(title, "xmp")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_XMPFS;
        } else if (strcasestr(title, "ssh")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_SSHFS;
        } else if (strcasestr(title, "ftp")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_FTPFS;
        } else if ((strcasestr(title, "webdav")) ||
                   (strcasestr(title, "wdfs"))) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_WEBDAVFS;
        } else if (strcasestr(title, "spotlight")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_SPOTLIGHTFS;
        } else if (strcasestr(title, "picasa")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_PICASAFS;
        } else if (strcasestr(title, "proc")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_PROCFS;
        } else if (strcasestr(title, "ntfs")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_NTFS;
        } else if (strcasestr(title, "beagle")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_BEAGLEFS;
        } else if (strcasestr(title, "crypto")) {
            *(uint32_t *)target = FUSE_FSSUBTYPE_CRYPTOFS;
        }

        free(title);
    }
    
    return 0;
}

static uint32_t  blocksize = FUSE_DEFAULT_BLOCKSIZE;
static uint32_t  fsid      = 0;
static char     *fsname    = NULL;
static uint32_t  iosize    = FUSE_DEFAULT_IOSIZE;
static uint32_t  subtype   = 0;
static char     *volname   = NULL;

struct mntval mvals[] = {
    {
        FUSE_MOPT_BLOCKSIZE,
        NULL,
        0,
        fuse_to_uint32,
        (void *)FUSE_DEFAULT_BLOCKSIZE,
        (void **)&blocksize,
        "invalid value for argument blocksize"
    },
    {
        FUSE_MOPT_FSID,
        NULL,
        0,
        fuse_to_fsid,
        0,
        (void **)&fsid,
        "invalid value for argument fsid (must be 0 < fsid < 0xFFFFFF)"
    },
    {
        FUSE_MOPT_FSNAME,
        NULL,
        0,
        fuse_to_string,
        NULL,
        (void **)&fsname,
        "invalid value for argument fsname"
    },
    {
        FUSE_MOPT_IOSIZE,
        NULL,
        0,
        fuse_to_uint32,
        (void *)FUSE_DEFAULT_IOSIZE,
        (void **)&iosize,
        "invalid value for argument iosize"
    },
    {
        FUSE_MOPT_SUBTYPE,
        NULL,
        0,
        fuse_to_subtype,
        NULL,
        (void **)&subtype,
        "invalid value for argument subtype"
    },
    {
        FUSE_MOPT_VOLNAME,
        NULL,
        0,
        fuse_to_string,
        NULL,
        (void **)&volname,
        "invalid value for argument volname"
    },
    {
        0, NULL, 0, NULL, (void *)NULL, (void **)NULL, (char *)NULL
    },
};

void
fuse_process_mvals()
{
    int ret;
    struct mntval *mv;

    for (mv = mvals; mv->mv_mntflag; mv++) {
        ret = mv->mv_converter(mv->mv_target, mv->mv_value, mv->mv_fallback);
        if (ret) {
            fprintf(stderr, "%s\n", mv->mv_errstr);
            exit(ret);
        }
    }
}

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

#include <DiskArbitration/DiskArbitration.h>

extern int DiskArbInit(void);
extern int DiskArbDiskAppearedWithMountpointPing_auto(
  char     *disk,
  unsigned  flags,
  char     *mountpoint
);

int
ping_diskarb(char *mntpath)
{
    int ret;
    struct statfs sb;
    enum {
        kDiskArbDiskAppearedEjectableMask   = 1 << 1,
        kDiskArbDiskAppearedNetworkDiskMask = 1 << 3
    };

    ret = statfs(mntpath, &sb);
    if (ret < 0) {
        return ret;
    }

    ret = DiskArbInit();

    /* we ignore the return value from DiskArbInit() */

    ret = DiskArbDiskAppearedWithMountpointPing_auto(
              sb.f_mntfromname,
              (kDiskArbDiskAppearedEjectableMask |
               kDiskArbDiskAppearedNetworkDiskMask),
              mntpath);

    /* we ignore the return value from the ping */

    return 0;
}

// We will be called as follows by the FUSE library:
//
//   mount_fusefs -o OPTIONS... <fdnam> <mountpoint>

int
main(int argc, char **argv)
{
    int       mntflags  = 0;
    int       fd        = -1;
    int32_t   index     = -1;
    char     *fdnam     = NULL;
    int       altflags  = 0;
    char     *mntpath   = NULL;

    int i, ch = '\0', done = 0;
    struct mntopt  *mo;
    struct mntval  *mv;
    fuse_mount_args args;
    
    if (!getenv("MOUNT_FUSEFS_CALL_BY_LIB")) {
        showhelp();
        /* NOTREACHED */
    }

    do {
        for (i = 0; i < 3; i++) {
            if (optind < argc && argv[optind][0] != '-') {
                if (mntpath) {
                    done = 1;
                    break;
                }
                if (fdnam)
                    mntpath = argv[optind];
                else
                    fdnam = argv[optind];
                optind++;
            }
        }

        switch(ch) {
        case 'o':
            getmntopts(optarg, mopts, &mntflags, &altflags);
            for (mv = mvals; mv->mv_mntflag; ++mv) {
                if (!(altflags & mv->mv_mntflag)) {
                    continue;
                }
                for (mo = mopts; mo->m_option; ++mo) {
                    char *p, *q;
                    if (mo->m_flag != mv->mv_mntflag) {
                        continue;
                    }
                    p = strstr(optarg, mo->m_option);
                    if (p) {
                        p += strlen(mo->m_option);
                        q = p;
                        while (*q != '\0' && *q != ',') {
                            q++;
                        }
                        mv->mv_len = q - p + 1;
                        mv->mv_value = malloc(mv->mv_len);
                        memcpy(mv->mv_value, p, mv->mv_len - 1);
                        ((char *)mv->mv_value)[mv->mv_len - 1] = '\0';
                        break;
                    }
                }
            }
            break;

        case '\0':
            break;

        case 'v': 
            showversion(1);
            break;

        case '?':
        case 'h':
        default:
            showhelp();
            break;
        }

        if (done) {
            break;
        }
    } while ((ch = getopt(argc, argv, "ho:v")) != -1);

    argc -= optind;
    argv += optind;

    if ((!fdnam) && argc > 0) {
        fdnam = *argv++;
        argc--;
    }

    if ((!mntpath) && argc > 0) {
        mntpath = *argv++;
        argc--;
    }

    if (!(fdnam && mntpath)) {
        errx(1, "missing FUSE fd name and/or mount point");
    }

    (void)checkpath(mntpath, args.mntpath);

    fuse_process_mvals();

    errno = 0;
    fd = strtol(fdnam, NULL, 10);
    if ((errno == EINVAL) || (errno == ERANGE)) {
        errx(1, "invalid name (%s) for FUSE device file descriptor", fdnam);
    } else {
        char  ndev[MAXPATHLEN];
        char *ndevbas;
        struct stat sb;

        if (fstat(fd, &sb) == -1) {
            err(1, "fstat failed for FUSE device file descriptor");
        }
        args.rdev = sb.st_rdev;
        strcpy(ndev, _PATH_DEV);
        ndevbas = ndev + strlen(_PATH_DEV);
        devname_r(sb.st_rdev, S_IFCHR, ndevbas,
                  sizeof(ndev) - strlen(_PATH_DEV));

        if (strncmp(ndevbas, FUSE_DEVICE_BASENAME,
                    strlen(FUSE_DEVICE_BASENAME))) {
            errx(1, "mounting inappropriate device");
        }

        errno = 0;
        index = strtol(ndevbas + strlen(FUSE_DEVICE_BASENAME), NULL, 10);
        if ((errno == EINVAL) || (errno == ERANGE) ||
            (index < 0) || (index > FUSE_NDEVICES)) {
            errx(1, "invalid FUSE device unit (#%d)\n", index);
        }
    }

    args.altflags  = altflags;
    args.blocksize = blocksize;
    args.fsid      = fsid;
    args.index     = index;
    args.iosize    = iosize;
    args.subtype   = subtype;

    if (!fsname) {
        snprintf(args.fsname, MAXPATHLEN, "instance@fuse%d", index);
    } else {
        snprintf(args.fsname, MAXPATHLEN, "%s", fsname);
    }

    if (!volname) {
        snprintf(args.volname, MAXPATHLEN, "FUSE Volume %d", index);
    } else {
        snprintf(args.volname, MAXPATHLEN, "%s", volname);
    }

    if (mount("fusefs", mntpath, mntflags, (void *)&args) < 0) {
        err(EX_OSERR, "fusefs@%d on %s", index, mntpath);
    }

    /*
     * XXX: There's a race condition here with the Finder. The kernel's
     * vfs_getattr() won't do the real thing until the daemon has responded
     * to the FUSE_INIT method. If the Finder does a stat on the file system
     * too soon, it will get "fake" information (leading to things like
     * "Zero KB on disk"). A decent solution is to do this pinging not here,
     * but somewhere else asynchronously, after we've made sure that the
     * kernel-user handshake is complete.
     */
    {
        pid_t pid;
        int wait_iterations = 60;

        signal(SIGCHLD, SIG_IGN);

        if ((pid = fork()) < 0) {
            err(EX_OSERR, "fusefs@%d on %s (fork failed)", index, mntpath);
        }

        setbuf(stderr, NULL);

        if (pid == 0) { /* child */
            for (; wait_iterations > 0; wait_iterations--) { 
                u_int32_t hs_complete = 0;
                int ret = ioctl(fd, FUSEDEVIOCISHANDSHAKECOMPLETE,
                                &hs_complete);
                if ((ret == 0) && hs_complete) {

                    if (args.altflags & FUSE_MOPT_PING_DISKARB) {
                        /* Let Disk Arbitration know. */
                        if (ping_diskarb(mntpath)) {
                            err(EX_OSERR, "fusefs@%d on %s (ping DiskArb)",
                                index, mntpath);
                        }
                    }

                    /* Let any notification listeners know. */
                    do {
                        CFStringRef nf_name, nf_object;
                        CFNotificationCenterRef distributedCenter;
                        CFStringEncoding encoding = kCFStringEncodingASCII;
                        
                        distributedCenter =
                            CFNotificationCenterGetDistributedCenter();
 
                        if (!distributedCenter) {
                            break;
                        }

                        nf_name = CFStringCreateWithCString(
                                     kCFAllocatorDefault,
                                     FUSE_UNOTIFICATIONS_MOUNTED, encoding);

                        nf_object = CFStringCreateWithCString(
                                        kCFAllocatorDefault,
                                        mntpath, encoding);

                        CFNotificationCenterPostNotification(
                            distributedCenter, nf_name, nf_object, NULL, false);

                        CFRelease(nf_name);
                        CFRelease(nf_object);

                    } while (0);
                    
                    exit(0);
                }
                usleep(FUSE_INIT_WAIT_INTERVAL);
            }
            err(EX_OSERR, "fusefs@%d on %s (gave up on init handshake)",
                index, mntpath);
        }
    }

    exit(0);
}

void
showhelp()
{
    if (!getenv("MOUNT_FUSEFS_CALL_BY_LIB")) {
        showversion(0);
        fprintf(stderr, "\nThis program is not meant to be called directly.\n");
    }
    fprintf(stderr, "\nAvailable mount options:\n");
    fprintf(stderr,
      "    -o blocksize=<size>    specify block size in bytes of \"storage\"\n"
      "    -o debug               turn on debug information printing\n"
      "    -o fsid                set the second 32-bit component of the fsid\n"
      "    -o fsname=<name>       set the file system's name\n"
      "    -o iosize=<size>       specify maximum I/O size in bytes\n" 
      "    -o jail_symlinks       contain symbolic links within the mount\n"
      "    -o noauthopaque        set MNTK_AUTH_OPAQUE in the kernel\n"
      "    -o noauthopaqueaccess  set MNTK_AUTH_OPAQUE_ACCESS in the kernel\n"
      "    -o noreadahead         disable I/O read-ahead behavior for this file system\n"
      "    -o nosyncwrites        disable synchronous-writes behavior (dangerous)\n"
      "    -o noubc               disable the unified buffer cache for this file system\n"
      "    -o ping_diskarb        ping Disk Arbitration\n"
      "    -o subtype=<num>       set the file system's subtype identifier\n"
      "    -o volname=<name>      set the file system's volume name\n"      
    );
    exit(EX_USAGE);
}

void
showversion(int doexit)
{
    fprintf(stderr, "Mac OS X FUSE version %s\n", MACFUSE_VERSION);
    if (doexit) {
        exit(EX_USAGE);
    }
}
