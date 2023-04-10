#include "evfs.h"
#include "cluster.h"
#include "view.h"
#include "entries.h"

#include "zmalloc.h"
#include "atom.h"
#include "threading.h"
#include "avltree.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

#define EVFS_ITERATOR_MAGIC ('retI')

enum evfs_entry_descriptor_state
{
    kEvfsDescriptorClosed = 0,
    kEvfsDescriptorCloseWait,
    kEvfsDescriptorActived,
};

struct evfs_entry_descriptor
{
    evfs_entry_handle_t handle;
    int entry_id;
    int offset;
    int refcnt;
    int stat;
    struct list_head element_of_descriptor_list;
    struct avltree_node_t leaf_of_descriptor_tree;
};

typedef struct evfs_interator {
    int magic;
    struct list_head *pos;
    int entry_id;
    int length;
    char key[MAX_ENTRY_NAME_LENGTH];
} evfs_interator_t;

static struct {
    int atom_handle;
    struct avltree_node_t *root_of_descriptors;
    struct list_head head_of_descriptors;
    int count_of_descriptors;
    lwp_mutex_t mutex;
} __evfs_descriptor_mgr = {
    .atom_handle = -1,
    .root_of_descriptors = NULL,
    .head_of_descriptors = { &__evfs_descriptor_mgr.head_of_descriptors, &__evfs_descriptor_mgr.head_of_descriptors },
    .count_of_descriptors = 0,
};

static int __evfs_descriptor_compare(const void *left, const void *right)
{
    const struct evfs_entry_descriptor *left_descriptor, *right_descriptor;

    left_descriptor = container_of(left, const struct evfs_entry_descriptor, leaf_of_descriptor_tree);
    right_descriptor = container_of(right, const struct evfs_entry_descriptor, leaf_of_descriptor_tree);

    if (left_descriptor->handle > right_descriptor->handle) {
        return 1;
    }

    if (left_descriptor->handle < right_descriptor->handle) {
        return -1;
    }

    return 0;
}

static void __evfs_queue_descriptor(struct evfs_entry_descriptor *descriptor)
{
    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    list_add_tail(&descriptor->element_of_descriptor_list, &__evfs_descriptor_mgr.head_of_descriptors);
    __evfs_descriptor_mgr.root_of_descriptors = avlinsert(__evfs_descriptor_mgr.root_of_descriptors, &descriptor->leaf_of_descriptor_tree, &__evfs_descriptor_compare);
    __evfs_descriptor_mgr.count_of_descriptors++;
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);
}

static void __evfs_dequeue_descriptor(struct evfs_entry_descriptor *descriptor)
{
    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    list_del_init(&descriptor->element_of_descriptor_list);
    __evfs_descriptor_mgr.root_of_descriptors = 
        avlremove(__evfs_descriptor_mgr.root_of_descriptors, &descriptor->leaf_of_descriptor_tree, NULL, &__evfs_descriptor_compare);
    __evfs_descriptor_mgr.count_of_descriptors--;
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);
}

static struct evfs_entry_descriptor *__evfs_search_descriptor(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor, find;
    struct avltree_node_t *found;

    find.handle = handle;
    found = avlsearch(__evfs_descriptor_mgr.root_of_descriptors, &find.leaf_of_descriptor_tree, &__evfs_descriptor_compare);
    if (!found) {
        return NULL;
    }

    descriptor = container_of(found, struct evfs_entry_descriptor, leaf_of_descriptor_tree);
    return  descriptor;
}

static struct evfs_entry_descriptor *__evfs_reference_descriptor(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;

    descriptor = NULL;

    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    descriptor = __evfs_search_descriptor(handle);
    if (descriptor) {
        if (kEvfsDescriptorActived == descriptor->stat) {
            descriptor->refcnt++;
        } else {
            descriptor = NULL;
        }
    }
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);

    return descriptor;
}

static void __evfs_dereference_descriptor(struct evfs_entry_descriptor *descriptor)
{
    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    if (descriptor->refcnt > 0) {
        descriptor->refcnt--;
    }
    if (0 == descriptor->refcnt && kEvfsDescriptorCloseWait == descriptor->stat) {
        __evfs_queue_descriptor(descriptor);
        zfree(descriptor);
    }
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);
}

static void __evfs_close_descriptor(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;

    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    descriptor = __evfs_search_descriptor(handle);
    if (descriptor) {
        if ( kEvfsDescriptorActived == descriptor->stat) {
            descriptor->stat = kEvfsDescriptorCloseWait;
        }
        if (0 == descriptor->refcnt) {
            __evfs_dequeue_descriptor(descriptor);
            zfree(descriptor);
        }
    }
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);
}

static nsp_boolean_t __evfs_is_legal_key(const char *key)
{
    const char *cursor;
    int i;

    if (!key) {
        return 0;
    }

    cursor = key;
    for (i = 0; i < MAX_ENTRY_NAME_LENGTH && (0 != *cursor); i++) {
        if ( (*cursor < 0x30 || *cursor > 0x39) && 
                (*cursor < 'a' || *cursor > 'z') && 
                (*cursor < 'A' && *cursor > 'Z') && *cursor != '_' && *cursor != '.') 
            {
                return 0;
            }
    }

    if (i == MAX_ENTRY_NAME_LENGTH  - 1) {
        return 0;
    }

    return 1;
}

nsp_status_t evfs_create(const char *path, int cluster_size_format, int cluster_count_format, int count_of_cache_cluster)
{
    nsp_status_t status;

    if ( atom_get(&__evfs_descriptor_mgr.atom_handle) > 0) {
        return EEXIST;
    }

    status = evfs_create_filesystem(path, cluster_size_format, cluster_count_format);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    status = evfs_view_create(count_of_cache_cluster);
    if (NSP_SUCCESS(status)) {
        evfs_entries_initial();
        lwp_mutex_init(&__evfs_descriptor_mgr.mutex, 1);
        atom_set(&__evfs_descriptor_mgr.atom_handle, 0);
    } else {
        evfs_close_filesystem();
    }

    return status;
}

nsp_status_t evfs_open(const char *path, int count_of_cache_cluster)
{
    nsp_status_t status;

    if ( atom_get(&__evfs_descriptor_mgr.atom_handle) > 0) {
        return EEXIST;
    }

    status = evfs_open_filesystem(path);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    status = evfs_view_load(evfs_entries_raw_recognize, count_of_cache_cluster);
    if (NSP_SUCCESS(status)) {
        evfs_entries_initial();
        lwp_mutex_init(&__evfs_descriptor_mgr.mutex, 1);
        atom_set(&__evfs_descriptor_mgr.atom_handle, 0);
    } else {
        evfs_close_filesystem();
    }
    
    return status;
}

void evfs_close()
{
    struct evfs_entry_descriptor *descriptor;

    if ( atom_get(&__evfs_descriptor_mgr.atom_handle) < 0) {
        return;
    }

    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);

    while (NULL != (descriptor = list_first_entry_or_null(&__evfs_descriptor_mgr.head_of_descriptors, struct evfs_entry_descriptor, element_of_descriptor_list))) {
        __evfs_close_descriptor(descriptor->handle);
    }

    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);

    evfs_entries_uninitial();
    evfs_view_cleanup();
    evfs_close_filesystem();

    atom_set(&__evfs_descriptor_mgr.atom_handle, -1);
}

nsp_status_t evfs_query_stat(evfs_stat_t *evstat)
{
    if (!evstat || atom_get(&__evfs_descriptor_mgr.atom_handle) < 0) {
        return posix__makeerror(EINVAL);
    }

    evstat->cluster_count = evfs_cluster_get_usable_count();
    evstat->cluster_size = evfs_cluster_get_size();
    evfs_view_get_count(&evstat->cluster_idle, &evstat->cluster_busy);
    evstat->entries = evfs_entries_query_total_count();
    return NSP_STATUS_SUCCESSFUL;
}

float evfs_query_cache_performance()
{
    return evfs_view_get_performance();
}

evfs_entry_handle_t evfs_create_entry(const char *key)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;

    if (!key || atom_get(&__evfs_descriptor_mgr.atom_handle) < 0) {
        return posix__makeerror(EINVAL);
    }

    if (!__evfs_is_legal_key(key)) {
        return posix__makeerror(EINVAL);
    }

    descriptor = (struct evfs_entry_descriptor *)ztrycalloc(sizeof(*descriptor));
    if (!descriptor) {
        return posix__makeerror(ENOMEM);
    }
    memset(descriptor, 0, sizeof(*descriptor));

    status = evfs_entries_create_one(key, &descriptor->entry_id);
    if (!NSP_SUCCESS(status)) {
        zfree(descriptor);
        return status;
    }
    descriptor->handle = atom_addone(&__evfs_descriptor_mgr.atom_handle);
    descriptor->offset = 0;
    descriptor->stat = kEvfsDescriptorActived;

    /* queue the descriptor */
    __evfs_queue_descriptor(descriptor);

    return descriptor->handle;
}

evfs_entry_handle_t evfs_open_entry(int entry_id)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;

    if (entry_id < 0 || atom_get(&__evfs_descriptor_mgr.atom_handle) < 0) {
        return posix__makeerror(EINVAL);
    }

    descriptor = (struct evfs_entry_descriptor *)ztrycalloc(sizeof(*descriptor));
    if (!descriptor) {
        return posix__makeerror(ENOMEM);
    }

    status = evfs_entries_open_one(entry_id);
    if (!NSP_SUCCESS(status)) {
        zfree(descriptor);
        return status;
    }
    descriptor->handle = atom_addone(&__evfs_descriptor_mgr.atom_handle);
    descriptor->entry_id = entry_id;
    descriptor->stat = kEvfsDescriptorActived;
    
    /* queue the descriptor */
    __evfs_queue_descriptor(descriptor);

    return descriptor->handle;
}

evfs_entry_handle_t evfs_open_entry_bykey(const char *key)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;

    if (!key || atom_get(&__evfs_descriptor_mgr.atom_handle) < 0) {
        return -1;
    }

    if (!__evfs_is_legal_key(key)) {
        return posix__makeerror(EINVAL);
    }

    descriptor = (struct evfs_entry_descriptor *)ztrycalloc(sizeof(*descriptor));
    if (!descriptor) {
        return -1;
    }

    status = evfs_entries_open_one_by_name(key, &descriptor->entry_id);
    if (!NSP_SUCCESS(status)) {
        zfree(descriptor);
        return -1;
    }
    descriptor->handle = atom_addone(&__evfs_descriptor_mgr.atom_handle);
    descriptor->stat = kEvfsDescriptorActived;

    /* queue the descriptor */
    __evfs_queue_descriptor(descriptor);

    return descriptor->handle;
}

int evfs_get_entry_size(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    int data_seg_size;

    if (handle < 0) {
        return -1;
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return -1;
    }

    data_seg_size = evfs_entries_query_user_seg_size(descriptor->entry_id);
    __evfs_dereference_descriptor(descriptor);
    return data_seg_size;
}

void evfs_close_entry(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;

    if (handle < 0) {
        return;
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return;
    }

    if (descriptor->entry_id > 0) {
        evfs_entries_close_one(descriptor->entry_id);
    }
    
    __evfs_close_descriptor(handle);
}

int evfs_write_entry(evfs_entry_handle_t handle, const char *data, int size)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;
    int wrcb;

    if ( handle <= 0 || !data || size <= 0) {
        return posix__makeerror(EINVAL);
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    do {
        status = evfs_entries_lock_elements(descriptor->entry_id, descriptor->offset, size);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        wrcb = evfs_entries_write_data(descriptor->entry_id, data, descriptor->offset, size);
        if (wrcb > 0) {
            descriptor->offset += wrcb;
        }
        status = wrcb;
    } while(0);
    __evfs_dereference_descriptor(descriptor);

    return status;
}

int evfs_read_entry(evfs_entry_handle_t handle, char *data, int size)
{
    struct evfs_entry_descriptor *descriptor;
    int rdcb;

    if (handle <= 0 || !data || size <= 0) {
        return posix__makeerror(EINVAL);
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    rdcb = evfs_entries_read_data(descriptor->entry_id, data, descriptor->offset, size);
    if (rdcb > 0) {
        descriptor->offset += rdcb;
    }
    __evfs_dereference_descriptor(descriptor);
    return rdcb;
}

int evfs_read_iterator_entry(const evfs_iterator_pt iterator, char *data, int size)
{
    if (!iterator || !data || size <= 0) {
        return posix__makeerror(EINVAL);
    }

    if (iterator->magic != EVFS_ITERATOR_MAGIC) {
        return posix__makeerror(EINVAL);
    }

    return evfs_entries_read_data(iterator->entry_id, data, 0, size);
}

int evfs_query_entry_length(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    int length;

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    length = evfs_entries_query_user_seg_size(descriptor->entry_id);

    __evfs_dereference_descriptor(descriptor);
    return length;
}

nsp_status_t evfs_seek_entry_offset(evfs_entry_handle_t handle, int seek)
{
    struct evfs_entry_descriptor *descriptor;

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }
    
    descriptor->offset = seek;
    __evfs_dereference_descriptor(descriptor);
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t evfs_truncate_entry(evfs_entry_handle_t handle, int size)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    status = evfs_entries_truncate(descriptor->entry_id, size);
    __evfs_dereference_descriptor(descriptor);
    return status;
}

void evfs_flush_entry_buffer(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return;
    }

    evfs_entries_flush(descriptor->entry_id);
    __evfs_dereference_descriptor(descriptor);
    return;
}

nsp_status_t evfs_earse_entry(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    status = evfs_entries_hard_delete(descriptor->entry_id);
    if (NSP_SUCCESS(status)) {
        descriptor->entry_id = -1;  /* this entry and it's descriptor are no longer usable */
    }
    __evfs_dereference_descriptor(descriptor);
    return status;
}

nsp_status_t evfs_earse_entry_by_name(const char *name)
{
    if (!name) {
        return posix__makeerror(EINVAL);
    }
    
    return evfs_entries_hard_delete_by_name(name);
}

evfs_iterator_pt evfs_iterate_entries(evfs_iterator_pt iterator)
{
    evfs_iterator_pt iter;

    iter = iterator;
    if (!iter) {
        iter = (evfs_iterator_pt)ztrycalloc(sizeof(*iter));
        if (!iter) {
            return NULL;
        }
        iter->magic = EVFS_ITERATOR_MAGIC;
        iter->pos = NULL;
        iter->entry_id = -1;
    }

    if (iter->magic != EVFS_ITERATOR_MAGIC) {
        return NULL;
    }

    iter->entry_id = evfs_entries_iterate(&iter->pos);
    if (iter->entry_id <= 0) {
        zfree(iter);
        return NULL;
    }
    evfs_entries_query_key(iter->entry_id, iter->key);
    iter->length = evfs_entries_query_user_seg_size(iter->entry_id);
    return iter;
}

void evfs_release_iterator(evfs_iterator_pt iterator)
{
    if (iterator) {
        if (iterator->magic == EVFS_ITERATOR_MAGIC) {
            zfree(iterator);
        }
    }
}

int evfs_get_iterator_entry_id(const evfs_iterator_pt iterator)
{
    if (iterator) {
        if (iterator->magic == EVFS_ITERATOR_MAGIC) {
            return iterator->entry_id;
        }
    }

    return -1;
}

const char *evfs_get_iterator_entry_key(const evfs_iterator_pt iterator)
{
    if (iterator) {
        if (iterator->magic == EVFS_ITERATOR_MAGIC) {
            return iterator->key;
        }
    }

    return NULL;
}

int evfs_get_iterator_entry_size(const evfs_iterator_pt iterator)
{
    if (iterator) {
        if (iterator->magic == EVFS_ITERATOR_MAGIC) {
            return iterator->length;
        }
    }

    return -1;
}

