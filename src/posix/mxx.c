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

/* use command: strings nshost.so.9.9.1 | grep 'COMPILE DATE'
    to query the compile date of specify ELF file */
#if _GENERATE_BY_CMAKE
#include "naxConfig.h"
nsp_status_t nis_getver(swnet_version_t *version)
{
    if (unlikely(!version)) {
        return posix__makeerror(EINVAL);
    }

    version->major = nax_VERSION_MAJOR;
    version->minor = nax_VERSION_MINOR;
    version->patch = nax_VERSION_PATCH;
    version->tweak = 0;
    snprintf(version->text, sizeof(version->text) - 1, "%d.%d.%d", nax_VERSION_MAJOR, nax_VERSION_MINOR, nax_VERSION_PATCH);
    return NSP_STATUS_SUCCESSFUL;
}
#else
nsp_status_t nis_getver(swnet_version_t *version)
{
    if (unlikely(!version)) {
        return posix__makeerror(EINVAL);
    }

    version->major = 9;
    version->minor = 9;
    version->patch = 1;
    version->tweak = 0;
    snprintf(version->text, sizeof(version->text) - 1, "%d.%d.%d", 9, 9, 1);
    return NSP_STATUS_SUCCESSFUL;
}
#endif
nsp_status_t nis_lgethost(abuff_64_t *name)
{
    ILLEGAL_PARAMETER_CHECK(!name);

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

    ILLEGAL_PARAMETER_CHECK (!name || !ipv4);

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
static nis_event_callback_fp _ecr = NULL;

nis_event_callback_fp nis_checr(const nis_event_callback_fp ecr)
{
    return __atomic_exchange_n(&_ecr, ecr, __ATOMIC_ACQ_REL);
}

/* the ecr usually use for diagnose low-level error */
void nis_call_ecr(const char *fmt,...)
{
    nis_event_callback_fp ecr;
    va_list ap;
    char logstr[1280];
    int retval;

    ILLEGAL_PARAMETER_STOP(!_ecr);

    va_start(ap, fmt);
    retval = vsnprintf(logstr, sizeof_array(logstr) - 1, fmt, ap);
    va_end(ap);
    if (retval <= 0) {
        return;
    }
    logstr[retval] = 0;

    /* double check the callback address */
    ecr = __atomic_load_n(&_ecr, __ATOMIC_ACQUIRE);
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

    ILLEGAL_PARAMETER_CHECK(!cbifv);
    ILLEGAL_PARAMETER_CHECK(*cbifv > 0 && !ifv);

    ifa = NULL;
    count = 0;

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

    ILLEGAL_PARAMETER_CHECK(link < 0);

    ncb = objrefr(link);
    if (!ncb) {
        return posix__makeerror(ENOENT);
    }

    retval = 0;

    va_start(ap, cmd);
    switch (cmd) {
        case NI_SETATTR:
            if (ncb->protocol == IPPROTO_TCP) {
                tcp_setattr_r(ncb, va_arg(ap, int));
            } else if (ncb->protocol == IPPROTO_UDP) {
                udp_setattr_r(ncb, va_arg(ap, int));
            } else {
                retval = -1;
            }
            break;
        case NI_GETATTR:
            retval = ncb_getattr_r(ncb);
            break;
        case NI_SETCTX:
            ncb->prcontext = __atomic_exchange_n(&ncb->context, va_arg(ap, const void *), __ATOMIC_ACQ_REL);
            break;
        case NI_GETCTX:
            __atomic_store_n(&context, ncb->context, __ATOMIC_RELEASE);
            *(va_arg(ap, void **) ) = context;
            break;
        case NI_SETTST:
            retval = tcp_settst_r(link, va_arg(ap, const void *));
            break;
        case NI_GETTST:
            retval = tcp_gettst_r(link, va_arg(ap, void *), NULL);
            break;
        case NI_GETAF:
            retval = ncb->local_addr.sin_family;
            break;
        case NI_GETPROTO:
            retval = ncb->protocol;
            break;
        case NI_GETRXTID:
            retval = (int)ncb->rx_tid;
            break;
        default:
            retval = posix__makeerror(EINVAL);
            break;
    }
    va_end(ap);

    objdefr(link);
    return retval;
}

nsp_status_t nis_getifmac(const char *eth_name, abuff_mac_t *phyaddr)
{
    struct ifreq ifr;
    int fd;

    ILLEGAL_PARAMETER_CHECK(!eth_name || !phyaddr);

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
