/*
 * Copyright (C) 2006 Google. All Rights Reserved.
 * Amit Singh <singh@>
 */

#ifndef _FUSE_PARAM_H_
#define _FUSE_PARAM_H_

#define M_MACFUSE_ENABLE_UNSUPPORTED 1
#define M_MACFUSE_ENABLE_XATTR       1

/*
 * This is the prefix ("fuse" by default) of the name of a FUSE device node
 * in devfs. The suffix is the device number. "/dev/fuse0" is the first FUSE
 * device by default. If you change the prefix from the default to something
 * else, the user-space FUSE library will need to know about it too.
 */
#define FUSE_DEVICE_BASENAME               "fuse"

/*
 * This is the number of /dev/fuse<n> nodes we will create. <n> goes from
 * 0 to (FUSE_NDEVICES - 1).
 */
#define FUSE_NDEVICES                      16

/*
 * This is the default block size of the virtual storage devices that are
 * implicitly implemented by the FUSE kernel extension. This can be changed
 * on a per-mount basis (there's one such virtual device for each mount).
 */
#define FUSE_DEFAULT_BLOCKSIZE             4096

#define FUSE_MIN_BLOCKSIZE                 512
#define FUSE_MAX_BLOCKSIZE                 MAXPHYS

#ifndef MAX_UPL_TRANSFER
#define MAX_UPL_TRANSFER 256
#endif

/*
 * This is default I/O size used while accessing the virtual storage devices.
 * This can be changed on a per-mount basis.
 *
 * Nevertheless, the I/O size must be at least as big as the block size.
 */
#define FUSE_DEFAULT_IOSIZE                (16 * PAGE_SIZE)

#define FUSE_MIN_IOSIZE                    512
#define FUSE_MAX_IOSIZE                    (MAX_UPL_TRANSFER * PAGE_SIZE)

#ifdef KERNEL

/*
 * This is the soft upper limit on the number of "request tickets" FUSE's
 * user-kernel IPC layer can have for a given mount. This can be modified
 * through the fuse.* sysctl interface.
 */
#define FUSE_DEFAULT_MAX_FREE_TICKETS      1024

#define FUSE_DEFAULT_IOV_PERMANENT_BUFSIZE (1L << 19)
#define FUSE_DEFAULT_IOV_CREDIT            16

#endif /* KERNEL */

#define FUSE_DEFAULT_INIT_TIMEOUT          10     /* s  */
#define FUSE_MIN_INIT_TIMEOUT              1      /* s  */
#define FUSE_MAX_INIT_TIMEOUT              300    /* s  */
#define FUSE_INIT_WAIT_INTERVAL            100000 /* us */

#define FUSE_DEFAULT_DAEMON_TIMEOUT        0      /* no timeout */
#define FUSE_MIN_DAEMON_TIMEOUT            0      /* s */
#define FUSE_MAX_DAEMON_TIMEOUT            600    /* s */

#define FUSE_UNMOUNT_DAEMON_TIMEOUT        20     /* s */

#define FUSE_MAXATTRIBUTESIZE              (128*1024)

#define FUSE_LINK_MAX                      LINK_MAX
#define FUSE_UIO_BACKUP_MAX                8

#endif /* _FUSE_PARAM_H_ */
