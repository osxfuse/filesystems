extern "C" {

#if __i386__

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "procfs_proc_info.h"

enum {
    PROC_INFO_LISTPIDS   = 1,
    PROC_INFO_PIDINFO    = 2,
    PROC_INFO_PIDFDINFO  = 3,
    PROC_INFO_KERNMSGBUF = 4,
};

extern int proc_pidinfo(int32_t pid, uint32_t flavor, uint64_t arg,
                        caddr_t buffer, int32_t buffersize);
extern int proc_pidfdinfo(int32_t pid, uint32_t flavor, uint32_t arg,
                          caddr_t buffer, int32_t buffersize);

static const char *
fd_type_string(uint32_t proc_fdtype)
{
    switch (proc_fdtype) {

    case PROX_FDTYPE_ATALK:
        return "AppleTalk";
        break;

    case PROX_FDTYPE_VNODE:
        return "vnode";
        break;

    case PROX_FDTYPE_SOCKET:
        return "socket";
        break;

    case PROX_FDTYPE_PSHM:
        return "POSIX shm";
        break;

    case PROX_FDTYPE_PSEM:
        return "POSIX sem";
        break;

    case PROX_FDTYPE_KQUEUE:
        return "kqueue";
        break;

    case PROX_FDTYPE_PIPE:
        return "pipe";
        break;

    case PROX_FDTYPE_FSEVENTS:
        return "fsevents";
        break;

    default:
        return "unknown descriptor type";
        break;
    }

    /* NOTREACHED */

    return "UNKNOWN";
}

static const char *socket_families[] = {
    "AF_UNSPEC",
    "AF_UNIX",
    "AF_INET",
    "AF_IMPLINK",
    "AF_PUP",
    "AF_CHAOS",
    "AF_NS",
    "AF_ISO",
    "AF_ECMA",
    "AF_DATAKIT",
    "AF_CCITT",
    "AF_SNA",
    "AF_DECnet",
    "AF_DLI",
    "AF_LAT",
    "AF_HYLINK",
    "AF_APPLETALK",
    "AF_ROUTE",
    "AF_LINK",
    "#define",
    "AF_COIP",
    "AF_CNT",
    "pseudo_AF_RTIP",
    "AF_IPX",
    "AF_SIP",
    "pseudo_AF_PIP",
    "pseudo_AF_BLUE",
    "AF_NDRV",
    "AF_ISDN",
    "pseudo_AF_KEY",
    "AF_INET6",
    "AF_NATM",
    "AF_SYSTEM",
    "AF_NETBIOS",
    "AF_PPP",
    "pseudo_AF_HDRCMPLT",
    "AF_RESERVED_36",
};
#define SOCKET_FAMILY_MAX (int)(sizeof(socket_families)/sizeof(char *))

static const char *
socket_family_string(int soi_family)
{
    if ((soi_family < 0) || (soi_family >= SOCKET_FAMILY_MAX)) {
        return "unknown socket family";
    }

    return socket_families[soi_family];
}

static const char *
socket_type_string(int soi_type)
{
    switch (soi_type) {

    case SOCK_STREAM:
        return "SOCK_STREAM";
        break;

    case SOCK_DGRAM:
        return "SOCK_DGRAM";
        break;

    case SOCK_RAW:
        return "SOCK_RAW";
        break;

    case SOCK_RDM:
        return "SOCK_RDM";
        break;

    case SOCK_SEQPACKET:
        return "SOCK_SEQPACKET";
        break;
    }

    /* NOTREACHED */

    return "unknown socket type";
}

int
procfs_proc_pidinfo(pid_t pid, char *buf, int *len)
{
    int ret;
    int orig_len = *len;
    int used_len = 0;

    ret = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, 0, 0);
    if (ret == -1) {
        return -1;
    } 
    
    size_t buffersize = (size_t)ret;

    char *buffer = (char *)malloc((size_t)buffersize);
    if (!buffer) {
        return -1;
    }

    ret = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, buffer,
                       (size_t)buffersize);

    struct proc_fdinfo *infop;
    int nfd = ret / sizeof(struct proc_fdinfo);
    infop = (struct proc_fdinfo *)buffer;

    int i;

    for (i = 0; i < nfd; i++) {

        if (used_len >= orig_len) {
            goto out;
        }

        used_len += snprintf(buf + used_len, orig_len - used_len,
                             "%-4d    %-10s ", infop[i].proc_fd,
                             fd_type_string(infop[i].proc_fdtype));

        switch (infop[i].proc_fdtype) {

        case PROX_FDTYPE_ATALK:
        case PROX_FDTYPE_FSEVENTS:
        case PROX_FDTYPE_KQUEUE:
        case PROX_FDTYPE_PIPE:
            if (used_len >= orig_len) {
                goto out;
            }
            used_len += snprintf(buf + used_len, orig_len - used_len, "\n");
            break;

        case PROX_FDTYPE_PSEM:
        {
            int nb;
            struct psem_fdinfo ps;
            if (used_len >= orig_len) {
                goto out;
            }
            nb = proc_pidinfo(pid, infop[i].proc_fd, PROC_PIDFDPSEMINFO,
                              (char *)&ps, sizeof(ps));
            if (nb <= 0) {
                used_len += snprintf(buf + used_len, orig_len - used_len, "\n");
            } else if ((unsigned int)nb < sizeof(ps)) {
                used_len += snprintf(buf + used_len, orig_len - used_len,
                                     "(%s)\n", "incomplete info");
            } else {
                if (ps.pseminfo.psem_name[0]) {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "%s\n", ps.pseminfo.psem_name);
                } else {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "\n");
                }
            }
            break;
        }

        case PROX_FDTYPE_PSHM:
        {
            int nb;
            struct pshm_fdinfo ps;
            if (used_len >= orig_len) {
                goto out;
            }
            nb = proc_pidinfo(pid, infop[i].proc_fd, PROC_PIDFDPSHMINFO,
                              (char *)&ps, sizeof(ps));
            if (nb <= 0) {
                used_len += snprintf(buf + used_len, orig_len - used_len, "\n");
            } else if ((unsigned int)nb < sizeof(ps)) {
                used_len += snprintf(buf + used_len, orig_len - used_len,
                                     "(%s)\n", "incomplete info");
            } else {
                if (ps.pshminfo.pshm_name[0]) {
                    ps.pshminfo.pshm_name[sizeof(ps.pshminfo.pshm_name) - 1] = '\0';
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "%s\n", ps.pshminfo.pshm_name);
                } else if (ps.pshminfo.pshm_mappaddr) {
                    /* ick */         
#define CAST_DOWN(type, addr) (((type)((uintptr_t)(addr))))
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "obj=%p\n",
                                         CAST_DOWN(void *, ps.pshminfo.pshm_mappaddr));
                } else {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "\n");
                }
            }
            break;
        }

        case PROX_FDTYPE_SOCKET:
        {
            int nb;
            struct socket_fdinfo si;
            if (used_len >= orig_len) {
                goto out;
            }
            nb = proc_pidfdinfo(pid, infop[i].proc_fd, PROC_PIDFDSOCKETINFO,
                                (char *)&si, sizeof(si));
            if (nb <= 0) {
                used_len += snprintf(buf + used_len, orig_len - used_len, "\n");
            } else if ((unsigned int)nb < sizeof(si)) {
                used_len += snprintf(buf + used_len, orig_len - used_len,
                                     "(%s)\n", "incomplete info");
            } else {
                struct protoent *proto = getprotobynumber(si.psi.soi_protocol);
                unsigned char *la = NULL, *fa = NULL;
                int lp = 0, fp = 0;
                if ((si.psi.soi_family == AF_INET) &&
                    (si.psi.soi_kind == SOCKINFO_TCP)) {
                    la = (unsigned char *)&si.psi.soi_proto.pri_tcp.tcpsi_ini.insi_laddr.ina_46.i46a_addr4;
                    lp = (int)ntohs(si.psi.soi_proto.pri_tcp.tcpsi_ini.insi_lport);
                    fa = (unsigned char *)&si.psi.soi_proto.pri_tcp.tcpsi_ini.insi_faddr.ina_46.i46a_addr4;
                    fp = (int)ntohs(si.psi.soi_proto.pri_tcp.tcpsi_ini.insi_fport);
                    char lh[MAXHOSTNAMELEN + 1] = { 0 };
                    char fh[MAXHOSTNAMELEN + 1] = { 0 };
                    char *h;
                    h = inet_ntoa(*(struct in_addr *)la);
                    if (h) {
                        snprintf(lh, MAXHOSTNAMELEN, "%s", h);
                    }
                    h = inet_ntoa(*(struct in_addr *)fa);
                    if (h) {
                        snprintf(fh, MAXHOSTNAMELEN, "%s", h);
                    }
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                  "%s, %s, %s, %s:%d --> %s:%d\n",
                                   socket_type_string(si.psi.soi_type),
                                   (proto) ? proto->p_name : "unknown protocol",
                                   socket_family_string(si.psi.soi_family),
                                   (lh[0]) ? lh : "unknown host", ntohs(lp),
                                   (fh[0]) ? fh : "unknown host", ntohs(fp));
                } else {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                  "%s, %s, %s\n",
                                   socket_type_string(si.psi.soi_type),
                                   (proto) ? proto->p_name : "unknown protocol",
                                   socket_family_string(si.psi.soi_family));
                }
            }
            break;
        }

        case PROX_FDTYPE_VNODE:
        {
            int nb;
            struct vnode_fdinfowithpath vi;
            nb = proc_pidfdinfo(pid, infop[i].proc_fd, PROC_PIDFDVNODEPATHINFO,
                                (char *)&vi, sizeof(vi));
            if (used_len >= orig_len) {
                goto out;
            }
            if (nb <= 0) {
                if (errno == ENOENT) {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "(%s)\n", "revoked");
                } else {
                    used_len += snprintf(buf + used_len, orig_len - used_len,
                                         "\n");
                }
            } else if ((unsigned int)nb < sizeof(vi)) {
                used_len += snprintf(buf + used_len, orig_len - used_len,
                                     "(%s)\n", "incomplete info");
            } else {
                used_len += snprintf(buf + used_len, orig_len - used_len,
                                     "%s\n", vi.pvip.vip_path);
            }
            break;
        }

        default:
            used_len += snprintf(buf + used_len, orig_len - used_len,
                                 "(%s)\n", "unknown descriptor type");
            break;
        }
    }
    
out:
    if (buffer) {
        free(buffer);
    }

    *len = used_len;

    return 0;
}

#endif /* __i386__ */

}
