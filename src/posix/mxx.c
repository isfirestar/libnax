/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
#include "mxx.h"

#include <ctype.h>
#include <stdarg.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#include "nisdef.h"
#include "ncb.h"

#include "tcp.h"
#include "udp.h"
#include "atom.h"

/* use command: strings nshost.so.9.9.1 | grep 'COMPILE DATE'
    to query the compile date of specify ELF file */
nsp_status_t nis_getver(swnet_version_t *version)
{
    static const char COMPILE_DATE[]="COMPILE DATE: Fri 14 May 2021 09:19:01 PM CST\n";
    size_t n;

    if (!version) {
        return NSP_STATUS_FATAL;
    }

    n = sizeof(version->compile_date);
    snprintf(version->compile_date, n, "%s", COMPILE_DATE);
    version->compile_date[n - 1] = 0;
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t nis_lgethost(abuff_64_t *name)
{
    if (!name) {
        return posix__makeerror(EINVAL);
    }

    if (0 == gethostname(name->st, sizeof(*name))) {
        return NSP_STATUS_SUCCESSFUL;
    }

    mxx_call_ecr("fatal error occurred syscall gethostname(2), error:%u", errno);
    return posix__makeerror(errno);
}

nsp_status_t nis_gethost(const char *name, uint32_t *ipv4)
{
    struct hostent *remote, ret;
    struct in_addr addr;
    int h_errnop;
    char buf[1024];
    int fr;

    if (!name || !ipv4) {
        return posix__makeerror(EINVAL);
    }

    *ipv4 = 0;
    remote = NULL;
    fr = 0;

    if (isalpha(name[0])) { /* host address is a name */
        fr = gethostbyname_r(name, &ret, buf, sizeof(buf), &remote, &h_errnop);
    } else {
        /*
        inet_aton() converts the Internet host address cp from the IPv4 numbers-and-dots notation into binary form (in network byte order)
                    and stores it in the structure that inp points to.
        inet_aton() returns nonzero if the address is valid, zero if not.  The address supplied in cp can have one of the following forms:
        a.b.c.d   Each of the four numeric parts specifies a byte of the address; the bytes are assigned in left-to-right order to produce the binary address.
        a.b.c     Parts a and b specify the first two bytes of the binary address.
                 Part c is interpreted as a 16-bit value that defines the rightmost two bytes of the binary address.
                 This  notation  is  suitable  for  specifying  (outmoded)  Class  B  network addresses.
        a.b       Part a specifies the first byte of the binary address.  Part b is interpreted as a 24-bit value that defines the rightmost three bytes of the binary address.  This notation is suitable for specifying (outmoded) Class A network addresses.
        a         The value a is interpreted as a 32-bit value that is stored directly into the binary address without any byte rearrangement.
        In  all  of  the  above forms, components of the dotted address can be specified in decimal, octal (with a leading 0), or hexadecimal, with a leading 0X).
        Addresses in any of these forms are collectively termed IPV4 numbers-and-dots notation.
        The form that uses exactly four decimal numbers is referred to as IPv4 dotted-decimal notation (or sometimes: IPv4 dotted-quad notation).
        inet_aton() returns 1 if the supplied string was successfully interpreted, or 0 if the string is invalid (errno is not set on error).
        */
        if (inet_aton(name, &addr)) {
            fr = gethostbyaddr_r(&addr, sizeof(addr), AF_INET, &ret, buf, sizeof(buf), &remote, &h_errnop);
        } else {
            return posix__makeerror(errno);
        }
    }

    if (0 != fr) {
        return posix__makeerror(fr);
    }

    if (!remote) {
        return NSP_STATUS_FATAL;
    }

    /* only IPv4 protocol supported */
    if (AF_INET != remote->h_addrtype) {
        return posix__makeerror(EPROTONOSUPPORT);
    }

    if (!remote->h_addr_list) {
        return posix__makeerror(ENOENT);
    }

    if (remote->h_length < sizeof (uint32_t)) {
        return NSP_STATUS_FATAL;
    }

    addr.s_addr = *((uint32_t *) remote->h_addr_list[0]);
    *ipv4 = ntohl(addr.s_addr);
    return NSP_STATUS_SUCCESSFUL;
}

/* manage ECR and it's calling */
static nis_event_callback_t __ecr = NULL;

nis_event_callback_t nis_checr(const nis_event_callback_t ecr)
{
    return atom_exchange(&__ecr, ecr);
}

/* the ecr usually use for diagnose low-level error */
void nis_call_ecr(const char *fmt,...)
{
    nis_event_callback_t ecr;
    va_list ap;
    char logstr[1280];
    int retval;

    if (!__ecr) {
        return;
    }

    va_start(ap, fmt);
    retval = vsnprintf(logstr, cchof(logstr) - 1, fmt, ap);
    va_end(ap);
    if (retval <= 0) {
        return;
    }
    logstr[retval] = 0;

    /* double check the callback address */
    ecr = atom_get(&__ecr);
    if (ecr) {
        ecr(logstr, NULL, 0);
    }
}

int nis_getifmisc(ifmisc_t *ifv, int *cbifv)
{
    struct ifaddrs *ifa, *ifs;
    int count;
    int i;
    int cbacquire;

    ifa = NULL;
    count = 0;

    if (!cbifv) {
        return -EINVAL;
    }

    if (*cbifv > 0 && !ifv) {
        return -EINVAL;
    }

    if (getifaddrs(&ifs) < 0) {
        return posix__makeerror(errno);
    }

    for (ifa = ifs; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr) {
            if(AF_INET == ifa->ifa_addr->sa_family ) {
                ++count;
            }
        }
    }

    do {
        cbacquire = count * sizeof(ifmisc_t);
        if (*cbifv < cbacquire) {
            *cbifv = cbacquire;
            i = -EAGAIN;
            break;
        }

        i = 0;
        for (ifa = ifs; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr) {
                if(AF_INET == ifa->ifa_addr->sa_family ) {
                    strncpy(ifv[i].eth, ifa->ifa_name, sizeof(ifv[i].eth) - 1);
                    ifv[i].inet = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
                    ifv[i].mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr.s_addr;
                    ifv[i].boardcast = ((struct sockaddr_in *)ifa->ifa_ifu.ifu_broadaddr)->sin_addr.s_addr;
                    i++;
                    if ( i == count) {
                        break;
                    }
                }
            }
        }
    } while (0);

    freeifaddrs(ifs);
    return i;
}

int nis_cntl(objhld_t link, int cmd, ...)
{
    ncb_t *ncb;
    int retval;
    va_list ap;
    void *context;

    if (link < 0) {
        return posix__makeerror(EINVAL);
    }

    if (NI_SINKCTX == cmd) {
        objdefr(link);
        return NSP_STATUS_SUCCESSFUL;
    }

    ncb = objrefr(link);
    if (!ncb) {
        return posix__makeerror(ENOENT);
    }

    retval = 0;

    va_start(ap, cmd);
    switch (cmd) {
        case NI_SETATTR:
            IPPROTO_TCP == ncb->protocol ? tcp_setattr_r(ncb, va_arg(ap, int)) :
                (IPPROTO_UDP == ncb->protocol ? udp_setattr_r(ncb, va_arg(ap, int)) : 0);
            break;
        case NI_GETATTR:
            IPPROTO_TCP == ncb->protocol ? tcp_getattr_r(ncb, &retval) :
                (IPPROTO_UDP == ncb->protocol ? udp_getattr_r(ncb, &retval) : 0);
            break;
        case NI_SETCTX:
            ncb->prcontext = atom_exchange(&ncb->context, va_arg(ap, const void *));
            break;
        case NI_GETCTX:
            atom_set(&context, ncb->context);
            *(va_arg(ap, void **) ) = context;
            break;
        case NI_SETTST:
            retval = tcp_settst_r(link, va_arg(ap, const void *));
            break;
        case NI_GETTST:
            retval = tcp_gettst_r(link, va_arg(ap, void *), NULL);
            break;
        case NI_RISECTX:
            atom_set(&context, ncb->context);
            *(va_arg(ap, void **) ) = context;
            link = INVALID_OBJHLD;
            break;
        case NI_GETAF:
            retval = ncb->local_addr.sin_family;
            break;
        case NI_GETPROTO:
            retval = ncb->protocol;
            break;
        default:
            retval = posix__makeerror(EINVAL);
            break;
    }
    va_end(ap);

    if (link > 0) {
        objdefr(link);
    }
    return retval;
}

nsp_status_t nis_getifmac(const char *eth_name, abuff_mac_t *phyaddr)
{
    struct ifreq ifr;
    int fd;

    if (!eth_name || !phyaddr) {
        return posix__makeerror(EINVAL);
    }

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd > 0) {
        strncpy(ifr.ifr_name, (char *)eth_name, sizeof(ifr.ifr_name) - 1 );
        if( ioctl(fd, SIOCGIFHWADDR, &ifr) >= 0) {

            /* struct sockaddr {
                   sa_family_t sa_family;
                   char        sa_data[14];
                } */
            memcpy(phyaddr, ifr.ifr_hwaddr.sa_data, sizeof(*phyaddr));
        }
        close(fd);
    }
    return posix__makeerror(errno);
}
