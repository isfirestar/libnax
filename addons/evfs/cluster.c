#include "cluster.h"
#include "evfs.h"

#include "atom.h"
#include "ifos.h"
#include "zmalloc.h"

#include <sys/mman.h>
#include <stdint.h>

static struct {
    file_descriptor_t fd;
    struct evfs_cluster *the_first_cluster;
    struct evfs_head_record_data *evhrd;
    int ready;
    int max_data_seg_size;
    int64_t fsize;
} __evfs_cluster_mgr = {
    .fd = INVALID_FILE_DESCRIPTOR,
    .the_first_cluster = NULL,
    .evhrd = NULL,
    .ready = kEvmgrNotReady,
    .max_data_seg_size = 0,
    .fsize = 0,
};

const char* evfs_simple_128bytes_cluster_data = "this string is a simple cluster data demo, it's 128 bytes long, this block are available for a evfs tester, create in 2023-04-14";

static nsp_boolean_t __evfs_is_ready()
{
    if (kEvmgrReady != atom_get(&__evfs_cluster_mgr.ready) ||
        !__evfs_cluster_mgr.evhrd || 
        !__evfs_cluster_mgr.the_first_cluster || 
        INVALID_FILE_DESCRIPTOR == __evfs_cluster_mgr.fd || 0 == __evfs_cluster_mgr.fsize || 0 == __evfs_cluster_mgr.max_data_seg_size)
    {
        return nsp_false;
    }

    return nsp_true;
}

/* calculate the byte offset by cluster id */
static int __evfs_cluster_offset(int cluster_id)
{
    return cluster_id * __evfs_cluster_mgr.evhrd->cluster_size;
}

/* expand file from offset to the target size 
 * we try to use large block which have 65536 bytes to stretch file when the remain bytes is large than or equal to it
*/
static nsp_status_t __evfs_cluster_expand_file(file_descriptor_t fd, int cluster_size, int offset, int size)
{
    unsigned char large_block[65536], *remainptr;
    int remain_size;
    nsp_status_t status;

    remain_size = size - offset;

    /* seek file to offset */
    status = ifos_file_seek(fd, offset);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    memset(large_block, 0, sizeof(large_block));
    while (remain_size >= sizeof(large_block)) {
        if (sizeof(large_block) != ifos_file_write(fd, large_block, sizeof(large_block))) {
            return posix__makeerror(EIO);
        }
        remain_size -= sizeof(large_block);
    }

    if (remain_size > 0) {
        remainptr = (unsigned char *)ztrymalloc(remain_size);
        if (!remainptr) {
            return posix__makeerror(ENOMEM);
        }
        memset(remainptr, 0, remain_size);

        if (remain_size != ifos_file_write(fd, remainptr, remain_size)) {
            status = posix__makeerror(EIO);
        } else {
            status = NSP_STATUS_SUCCESSFUL;
        }
        zfree(remainptr);
    }

    if (NSP_SUCCESS(status)) {
        status = ifos_file_flush(fd);
    }

    return status;
}

nsp_status_t evfs_hard_create(const char *file, int cluster_size_format, int cluster_count_format)
{
    int expect;
    int cluster_size;
    nsp_status_t status;
    struct evfs_head_record_data *evhrd;
    struct evfs_cluster *clusterptr;
    int wrcb;
    file_descriptor_t fd;

    /* double initialize */
    expect = kEvmgrNotReady;
    if (!atom_compare_exchange_strong(&__evfs_cluster_mgr.ready, &expect, kEvmgrInitializing)) {
        return posix__makeerror(EEXIST);
    }

    evhrd = NULL;
    clusterptr = NULL;
    fd = INVALID_FILE_DESCRIPTOR;
    do {
        if (cluster_size_format < MINIMUM_CLUSTER_SIZE || cluster_size_format > MAXIMUM_CLUSTER_SIZE ||
            !is_powerof_2(cluster_size_format)) 
        {
            status = posix__makeerror(EINVAL);
            break;
        }
        cluster_size = cluster_size_format;
        

        if (cluster_size * cluster_count_format >= MAXIMUM_FILE_SIZE) {
            status = posix__makeerror(EINVAL);
            break;
        }

        clusterptr = (struct evfs_cluster *)ztrymalloc(cluster_size);
        if (!clusterptr) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        memset(clusterptr, 0, cluster_size);

        status = ifos_file_open(file, FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* create hrd */
        clusterptr->data_seg_size = sizeof(*evhrd);
        clusterptr->next_cluster_id = 0;
        clusterptr->head_cluster_id = 0;
        evhrd = (struct evfs_head_record_data *)((char *)clusterptr + sizeof(*clusterptr));
        evhrd->cluster_count = cluster_count_format;
        evhrd->cluster_size = cluster_size;
        evhrd->expand_cluster_count = cluster_count_format;
        evhrd->magic = EVFS_MAGIC;
        wrcb = ifos_file_write(fd, clusterptr, cluster_size);
        if (wrcb != cluster_size) {
            status = posix__makeerror(wrcb);
            break;
        }

        /* expand file to expect size */
        status = __evfs_cluster_expand_file(fd, cluster_size_format, cluster_size_format, cluster_size_format * cluster_count_format);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* all ok. fill the manger filed */
        __evfs_cluster_mgr.the_first_cluster = clusterptr;
        __evfs_cluster_mgr.evhrd = evhrd;
        __evfs_cluster_mgr.max_data_seg_size = __evfs_cluster_mgr.evhrd->cluster_size - sizeof(struct evfs_cluster);
        __evfs_cluster_mgr.fd = fd;
        __evfs_cluster_mgr.fsize = cluster_size * cluster_count_format;
        atom_set(&__evfs_cluster_mgr.ready, kEvmgrReady);
        status = NSP_STATUS_SUCCESSFUL;

    } while(0);

    if (clusterptr && clusterptr != __evfs_cluster_mgr.the_first_cluster) {
        zfree(clusterptr);
    }
    if (fd > 0 && fd != __evfs_cluster_mgr.fd) {
        ifos_file_close(fd);
    }

    /* reset ready state when failed. */
    expect = kEvmgrInitializing;
    atom_compare_exchange_strong(&__evfs_cluster_mgr.ready, &expect, kEvmgrNotReady);
    return status;
}

/* check the loaded file system from specify file are legal */
static nsp_status_t __evfs_check_hard_legal(const struct evfs_head_record_data *evhrd, int fsize)
{
    if (evhrd->magic != EVFS_MAGIC) {
        return posix__makeerror(EBFONT);
    }

    if (evhrd->cluster_size < MINIMUM_CLUSTER_SIZE || evhrd->cluster_size > MAXIMUM_CLUSTER_SIZE) {
        return posix__makeerror(EBFONT);
    }

    if (evhrd->cluster_size * evhrd->cluster_count != fsize) {
        return posix__makeerror(EBFONT);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t evfs_hard_open(const char *file)
{
    int fd;
    nsp_status_t status;
    int expectval;
    struct evfs_cluster *clusterptr, *guide;
    struct evfs_head_record_data *evhrd;
    int64_t fsize;
    int rdcb;
    int guide_size;

    /* double initialize */
    expectval = kEvmgrNotReady;
    if (!atom_compare_exchange_strong(&__evfs_cluster_mgr.ready, &expectval, kEvmgrInitializing)) {
        return posix__makeerror(EEXIST);
    }

    fd = INVALID_FILE_DESCRIPTOR;
    clusterptr = NULL;
    guide = NULL;
    guide_size = sizeof(*clusterptr) + sizeof(*evhrd);
    do {
        status = ifos_file_open(file, FF_WRACCESS | FF_OPEN_EXISTING, 0644, &fd);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        fsize = ifos_file_fgetsize(fd);
        if (fsize < guide_size || fsize > MAXIMUM_FILE_SIZE) {
            status = posix__makeerror(EBFONT);
            break;
        }

        guide = (struct evfs_cluster *)ztrymalloc(guide_size);
        if (!guide) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        
         /* read the first cluster as hdr */
        rdcb = ifos_file_read(fd, guide, guide_size);
        if (rdcb != guide_size) {
            status = posix__makeerror(rdcb);
            break;
        }

        if (guide->data_seg_size != sizeof(*evhrd) || guide->next_cluster_id != 0 || guide->head_cluster_id != 0) 
        {
            status = posix__makeerror(EPROTOTYPE);
            break;
        }
        evhrd = (struct evfs_head_record_data *)((char *)guide + sizeof(*clusterptr));
        status = __evfs_check_hard_legal(evhrd, fsize);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        clusterptr = (struct evfs_cluster *)ztrymalloc(evhrd->cluster_size);
        if (!clusterptr) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        memcpy(clusterptr, guide, guide_size);
        zfree(guide);
        guide = NULL;
        
        /* all ok, fill manager */
        __evfs_cluster_mgr.the_first_cluster = clusterptr;
        __evfs_cluster_mgr.evhrd = (struct evfs_head_record_data *)clusterptr->data;
        __evfs_cluster_mgr.max_data_seg_size = __evfs_cluster_mgr.evhrd->cluster_size - sizeof(struct evfs_cluster);
        __evfs_cluster_mgr.fd = fd;
        __evfs_cluster_mgr.fsize = fsize;
        atom_set(&__evfs_cluster_mgr.ready, kEvmgrReady);
        status = NSP_STATUS_SUCCESSFUL;

    } while(0);

    if (guide) {
        zfree(guide);
    }
    if (clusterptr && clusterptr != __evfs_cluster_mgr.the_first_cluster) {
        zfree(clusterptr);
    }
    if (fd > 0 && fd != __evfs_cluster_mgr.fd) {
        ifos_file_close(fd);
    }
    /* reset ready state when failed. */
    expectval = kEvmgrInitializing;
    atom_compare_exchange_strong(&__evfs_cluster_mgr.ready, &expectval, kEvmgrNotReady);
    return status;
}

void evfs_hard_close()
{
    atom_set(&__evfs_cluster_mgr.ready, kEvmgrClosing);

    if (__evfs_cluster_mgr.fd > 0) {
        ifos_file_flush(__evfs_cluster_mgr.fd);
        ifos_file_close(__evfs_cluster_mgr.fd);
        __evfs_cluster_mgr.fd = INVALID_FILE_DESCRIPTOR;
    }

    if (__evfs_cluster_mgr.the_first_cluster) {
        zfree(__evfs_cluster_mgr.the_first_cluster);
        __evfs_cluster_mgr.the_first_cluster = NULL;
    }
    __evfs_cluster_mgr.evhrd = NULL;

    atom_set(&__evfs_cluster_mgr.ready, kEvmgrNotReady);
}

int evfs_hard_expand(int *expanded_head_cluster_id)
{
    nsp_status_t status;
    int expand_size;
    int available_cluster_count;
    
    if (!__evfs_is_ready()) {
        return posix__makeerror(EBFONT);
    }

    /* expand physical file */
    available_cluster_count = __evfs_cluster_mgr.evhrd->cluster_count;
    expand_size = __evfs_cluster_mgr.evhrd->expand_cluster_count * __evfs_cluster_mgr.evhrd->cluster_size;
    status = __evfs_cluster_expand_file(__evfs_cluster_mgr.fd, __evfs_cluster_mgr.evhrd->cluster_size, __evfs_cluster_mgr.fsize, __evfs_cluster_mgr.fsize + expand_size);
    if (!NSP_SUCCESS(status)) {
        return status;
    }
    __evfs_cluster_mgr.fsize += expand_size;
    __evfs_cluster_mgr.evhrd->cluster_count += __evfs_cluster_mgr.evhrd->expand_cluster_count;

    /* update the first cluster in harddisk */
    status = evfs_hard_write_cluster(0, __evfs_cluster_mgr.the_first_cluster);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (expanded_head_cluster_id) {
        *expanded_head_cluster_id = available_cluster_count;
    }

    return __evfs_cluster_mgr.evhrd->expand_cluster_count;
}

nsp_status_t evfs_hard_write_cluster(int cluster_id, const void *buffer)
{
    int wrcb;
    int off;
    nsp_status_t status;

    if (!__evfs_is_ready()) {
        return posix__makeerror(EBFONT);
    }

    off = __evfs_cluster_offset(cluster_id);
    status = ifos_file_seek(__evfs_cluster_mgr.fd, off);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    wrcb = ifos_file_write(__evfs_cluster_mgr.fd, buffer, __evfs_cluster_mgr.evhrd->cluster_size);
    return wrcb == __evfs_cluster_mgr.evhrd->cluster_size ? NSP_STATUS_SUCCESSFUL : wrcb;
}

nsp_status_t evfs_hard_read_cluster(int cluster_id, void *buffer)
{
    int rdcb; 
    int off;
    nsp_status_t status;

    if (!__evfs_is_ready()) {
        return posix__makeerror(EBFONT);
    }

    off = __evfs_cluster_offset(cluster_id);
    status = ifos_file_seek(__evfs_cluster_mgr.fd, off);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    rdcb = ifos_file_read(__evfs_cluster_mgr.fd, buffer, __evfs_cluster_mgr.evhrd->cluster_size);
    return rdcb == __evfs_cluster_mgr.evhrd->cluster_size ? NSP_STATUS_SUCCESSFUL : rdcb;
}

nsp_status_t evfs_hard_read_cluster_head(int cluster_id, struct evfs_cluster *clusterptr)
{
    int rdcb; 
    int off;
    nsp_status_t status;

    if (!__evfs_is_ready()) {
        return posix__makeerror(EBFONT);
    }

    off = __evfs_cluster_offset(cluster_id);
    status = ifos_file_seek(__evfs_cluster_mgr.fd, off);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    rdcb = ifos_file_read(__evfs_cluster_mgr.fd, clusterptr, sizeof(struct evfs_cluster));
    return rdcb == sizeof(struct evfs_cluster) ? NSP_STATUS_SUCCESSFUL : rdcb;
}

nsp_status_t evfs_hard_flush()
{
    if (!__evfs_is_ready()) {
        return posix__makeerror(EBFONT);
    }

    return ifos_file_flush(__evfs_cluster_mgr.fd);
}

int evfs_hard_get_filesize()
{
    return __evfs_is_ready() ? __evfs_cluster_mgr.fsize : 0;
}

int evfs_hard_get_cluster_size()
{
    return __evfs_is_ready() ? __evfs_cluster_mgr.evhrd->cluster_size : 0;
}

int evfs_hard_get_usable_cluster_count()
{
    return __evfs_is_ready() ? atom_get(&__evfs_cluster_mgr.evhrd->cluster_count) - 1 : 0;
}

int evfs_hard_get_max_data_seg_size()
{
    return __evfs_is_ready() ? __evfs_cluster_mgr.max_data_seg_size : 0;
}

evfs_cluster_pt evfs_hard_allocate_cluster(const evfs_cluster_pt source)
{
    struct evfs_cluster *clusterptr;

    clusterptr = NULL;
    if (__evfs_is_ready()) {
        clusterptr = (struct evfs_cluster *)ztrymalloc(sizeof(*clusterptr) + __evfs_cluster_mgr.evhrd->cluster_size);
        if (!clusterptr) {
            return NULL;
        }
        memset(clusterptr, 0, sizeof(*clusterptr) + __evfs_cluster_mgr.evhrd->cluster_size); 
        if (source) {
            memcpy(clusterptr, source, sizeof(*source));
        }
    }
    return clusterptr;
}

void evfs_hard_release_cluster(struct evfs_cluster *clusterptr)
{
    if (clusterptr) {
        zfree(clusterptr);
    }
}
