#include "cache.h"

#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "threading.h"
#include "clock.h"

enum evfs_cache_node_state {
    kEvfsCacheBlockIdle = 0,
    kEvfsCacheBlockClean,
    kEvfsCacheBlockDirty,
    kEvfsCacheBlockUnknown = 0xff,
};

struct evfs_cache_node {
    struct avltree_node_t leaf_of_lru_index;   /* index cache by  cluster_id */
    union {
        struct list_head element_of_lru;            /* element of lru list */
        struct list_head element_of_idle;           /* element of idle list */
    };
    struct list_head element_of_dirty;          /* element of dirty list */
    int cluster_id;
    int state;
    unsigned char *data;
};

struct evfs_cache_efficiency_statistcs {
    int count_of_hit;
    int count_of_miss;
};

struct list_with_count {
    struct list_head head;
    int count;
};

enum evfs_cache_iotype
{
    kEvfsCacheIOTypeRead = 0,
    kEvfsCacheIOTypeReadHeadDirectly,
    kEvfsCacheIOTypeReadDirectly,
    kEvfsCacheIOTypeWrite,
    kEvfsCacheIOTypeWriteDirectly,
    kEvfsCacheIOTypeFlushBuffer,
    kEvfsCacheIOTypeFlushBufferAll,
    kEvfsCacheIOTypeExpandFile,
    kEvfsCacheIOTypeAddCacheBlock,
    kEvfsCacheIOTypeHardClose,
};

#define EVFS_MAX_IO_PENDING_COUNT   (160)

struct evfs_cache_io_task {
    struct list_head element;
    int cluster_id;
    int offset;
    int length;
    union {
        unsigned char *data;
        const unsigned char *cdata;
        void *ptr;
    };
    enum evfs_cache_iotype io_type;
    nsp_status_t status;
    lwp_event_t cond;
    int no_wait;
};

struct evfs_cache_merge_thread
{
    lwp_t thread;
    lwp_event_t cond;
    int stop;
    struct list_with_count io;
    lwp_mutex_t mutex;
};

static struct {
    struct avltree_node_t *root_of_lru_index;
    struct list_with_count lru;
    struct list_with_count dirty;
    struct list_with_count idle;
    int cluster_size;
    int cache_cluster_count;
    struct evfs_cache_efficiency_statistcs statistics;
    struct evfs_cache_merge_thread merge;
    int ready;
} __evfs_cache_mgr = {
    .root_of_lru_index = NULL,
    .lru = { LIST_HEAD_INIT(__evfs_cache_mgr.lru.head), 0 },
    .dirty = { LIST_HEAD_INIT(__evfs_cache_mgr.dirty.head), 0 },
    .idle = { LIST_HEAD_INIT(__evfs_cache_mgr.idle.head), 0 },
    .cluster_size = 0,
    .cache_cluster_count = 0,
    .statistics = { 0, 0 },
    .merge = { .
        thread = LWP_TYPE_INIT,
        .stop = 0,
        .io = { LIST_HEAD_INIT(__evfs_cache_mgr.merge.io.head), 0 },
        .mutex = { PTHREAD_MUTEX_INITIALIZER } },
    .ready = 0,
};

static nsp_status_t __evfs_cache_push_task_and_notify_run(struct evfs_cache_io_task *task, int no_wait);
static void *__evfs_cache_thread_merge_io(void *parameter);
static nsp_status_t __evfs_cache_wait_background_compelete(struct evfs_cache_io_task *task);
static void __evfs_cache_flush_buffer_all();

int __evfs_cache_compare_by_cluster_id(const void *left, const void *right)
{
    struct evfs_cache_node *l = container_of(left, struct evfs_cache_node, leaf_of_lru_index);
    struct evfs_cache_node *r = container_of(right, struct evfs_cache_node, leaf_of_lru_index);

    if (l->cluster_id < r->cluster_id)
        return -1;
    else if (l->cluster_id > r->cluster_id)
        return 1;
    else
        return 0;
}

/* flush cache block data to harddisk if this block is dirty */
static void __evfs_cache_flush_block_if_dirty(struct evfs_cache_node *node)
{
    if (node->state == kEvfsCacheBlockDirty) {
        evfs_hard_write_cluster(node->cluster_id, node->data);
        node->state = kEvfsCacheBlockClean;
        /* remove from dirty list */
        list_del(&node->element_of_dirty);
        __evfs_cache_mgr.dirty.count--;
    }
}

/* search block in lru list by indexer tree */
struct evfs_cache_node *__evfs_cache_search_lru(int cluster_id)
{
    struct evfs_cache_node key, *node;
    struct avltree_node_t *found;

    key.cluster_id = cluster_id;
    found = avlsearch(__evfs_cache_mgr.root_of_lru_index, &key.leaf_of_lru_index, __evfs_cache_compare_by_cluster_id);
    if (!found) {
        return NULL;
    }

    node = container_of(found, struct evfs_cache_node, leaf_of_lru_index);
    return node;
}

/* pop a node from idle list */
static struct evfs_cache_node *__evfs_cache_pop_idle(void)
{
    struct evfs_cache_node *node;

    node = NULL;

    if (!list_empty(&__evfs_cache_mgr.idle.head)) {
        node = container_of(__evfs_cache_mgr.idle.head.next, struct evfs_cache_node, element_of_lru);
        list_del(&node->element_of_lru);
        __evfs_cache_mgr.idle.count--;
    }
    return node;
}

/* pop a node from head of lru list */
static struct evfs_cache_node *__evfs_cache_pop_lru(void)
{
    struct evfs_cache_node *node;

    node = NULL;

    if (!list_empty(&__evfs_cache_mgr.lru.head)) {
        node = container_of(__evfs_cache_mgr.lru.head.next, struct evfs_cache_node, element_of_lru);
        list_del(&node->element_of_lru);
        __evfs_cache_mgr.root_of_lru_index = avlremove(__evfs_cache_mgr.root_of_lru_index, &node->leaf_of_lru_index, NULL, __evfs_cache_compare_by_cluster_id);
        __evfs_cache_mgr.lru.count--;
    }
    return node;
}

/* add a node to the tail of idle list */
static void __evfs_cache_push_idle(struct evfs_cache_node *node)
{
    node->cluster_id = 0;
    node->state = kEvfsCacheBlockIdle;
    list_add_tail(&node->element_of_idle, &__evfs_cache_mgr.idle.head);
    __evfs_cache_mgr.idle.count++;
}

/* set a node state to dirty and push it to the dirty list */
static void __evfs_cache_push_dirty(struct evfs_cache_node *node)
{
    if (kEvfsCacheBlockDirty != node->state) {
        node->state = kEvfsCacheBlockDirty;
        list_add_tail(&node->element_of_dirty, &__evfs_cache_mgr.dirty.head);
        __evfs_cache_mgr.dirty.count++;
    }
}

/* push a node to the tail of lru list */
static void __evfs_cache_push_lru(struct evfs_cache_node *node)
{
    list_add_tail(&node->element_of_lru, &__evfs_cache_mgr.lru.head);
    __evfs_cache_mgr.lru.count++;
    __evfs_cache_mgr.root_of_lru_index = avlinsert(__evfs_cache_mgr.root_of_lru_index, &node->leaf_of_lru_index, __evfs_cache_compare_by_cluster_id);
}

/* move the specify node to the tail of lru list */
static void __evfs_cache_move_lru_to_tail(struct evfs_cache_node *node)
{
    list_del_init(&node->element_of_lru);
    list_add_tail(&node->element_of_lru, &__evfs_cache_mgr.lru.head);
}

/* try to read data from existing cache node
 * if node not found, process as cache miss and return error code */
static nsp_status_t __evfs_cache_read_from_lru(int cluster_id, unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;

    node = __evfs_cache_search_lru(cluster_id);
    if (!node) {
        /* increase hit miss number */
        __evfs_cache_mgr.statistics.count_of_miss++;
        return posix__makeerror(ENOENT);
    }

    /* move to tail of cached list */
    __evfs_cache_move_lru_to_tail(node);
    /* copy data to user buffer */
    memcpy(data, node->data + offset, length);
    /* increase hit number */
    __evfs_cache_mgr.statistics.count_of_hit++;

    return NSP_STATUS_SUCCESSFUL;
}

/* try to write data to existing cache node
 * if node not found, process as cache miss and return error code
 */
static nsp_status_t __evfs_cache_write_to_lru(int cluster_id, const unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;

    node = __evfs_cache_search_lru(cluster_id);
    if (!node) {
        /* increase hit miss number */
        __evfs_cache_mgr.statistics.count_of_miss++;
        return posix__makeerror(ENOENT);
    }

    /* move to tail of cached list */
    __evfs_cache_move_lru_to_tail(node);
    /* copy data to user buffer */
    memcpy(node->data + offset, data, length);
    /* increase hit number */
    __evfs_cache_mgr.statistics.count_of_hit++;
    /* chage node state to dirty */
    __evfs_cache_push_dirty(node);

    return NSP_STATUS_SUCCESSFUL;
}

/* try to fetch node from idle list and read from harddisk,
 * on success, save this node to lru list and return successful, otherwise return error code */
static nsp_status_t __evfs_cache_read_from_idle(int cluster_id, unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;
    nsp_status_t status;

    node = __evfs_cache_pop_idle();
    if (!node) {
        return posix__makeerror(ENOMEM);
    }

    /* read data from harddisk */
    status = evfs_hard_read_cluster(cluster_id, node->data);
    if (!NSP_SUCCESS(status)) {
        __evfs_cache_push_idle(node);  /* recycle to idle */
        return status;
    }
    /* set node cluster id */
    node->cluster_id = cluster_id;
    /* set node state to clean */
    node->state = kEvfsCacheBlockClean;
    /* copy data to user buffer */
    memcpy(data, node->data + offset, length);
    /* push to lru list */
    __evfs_cache_push_lru(node);

    return NSP_STATUS_SUCCESSFUL;
}

/* try to fetch node from idle list
 * if there are idle node, read from harddisk, cover user data to node cache and save to lru list
 * on success, save this node to lru list and return successful, otherwise return error code */
static nsp_status_t __evfs_cache_write_to_idle(int cluster_id, const unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;
    nsp_status_t status;

    node = __evfs_cache_pop_idle();
    if (!node) {
        return posix__makeerror(ENOMEM);
    }

    /* read data from harddisk */
    status = evfs_hard_read_cluster(cluster_id, node->data);
    if (!NSP_SUCCESS(status)) {
        return status;
    }
    /* set node cluster id */
    node->cluster_id = cluster_id;
    /* copy data to user buffer */
    memcpy(node->data + offset, data, length);
    /* push to dirty list */
    __evfs_cache_push_dirty(node);
    /* push to lru list */
    __evfs_cache_push_lru(node);

    return status;
}

/* read cluster data from harddisk, and replace the head node of lru list
 * if the replace node state is dirty, write it to harddisk
 * the node will be move to tail lru list and use to save user data in which cluster specify now
*/
static nsp_status_t __evfs_cache_replace_read_from_lru(int cluster_id, unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;
    nsp_status_t status;

    node = __evfs_cache_pop_lru();
    if (!node) {
        return posix__makeerror(ENOENT);
    }

    /* if node state is dirty, write it to harddisk */
    __evfs_cache_flush_block_if_dirty(node);
    /* load cluster data from harddisk */
    status = evfs_hard_read_cluster(cluster_id, node->data);
    if (!NSP_SUCCESS(status)) {
        __evfs_cache_push_idle(node); /* recycle to idle */
        return status;
    }

    /* change cluster id of this node */
    node->cluster_id = cluster_id;
    /* set node state to clean */
    node->state = kEvfsCacheBlockClean;
    /* copy data to user buffer */
    memcpy(data, node->data + offset, length);
    /* push to lru list */
    __evfs_cache_push_lru(node);
    return status;
}

/* write cluster data to harddisk, and replace the head node of lru list
 * if the replace node state is dirty, write it to harddisk
 * the node will be move to tail lru list and use to save user data in which cluster specify now */
static nsp_status_t __evfs_cache_replace_write_to_lru(int cluster_id, const unsigned char *data, int offset, int length)
{
    struct evfs_cache_node *node;
    nsp_status_t status;

    node = __evfs_cache_pop_lru();
    if (!node) {
        return posix__makeerror(ENOENT);
    }

    /* if node state is dirty, write it to harddisk */
    __evfs_cache_flush_block_if_dirty(node);
    /* load cluster data from harddisk */
    status = evfs_hard_read_cluster(cluster_id, node->data);
    if (!NSP_SUCCESS(status)) {
        __evfs_cache_push_idle(node); /* recycle to idle */
        return status;
    }

    /* change cluster id of this node */
    node->cluster_id = cluster_id;
    /* copy data to user buffer */
    memcpy(node->data + offset, data, length);
    /* set node state to dirty */
    __evfs_cache_push_dirty(node);
    /* push to lru list */
    __evfs_cache_push_lru(node);
    return status;
}

/* read from harddisk directly, this case only run when cache block specify to zero */
static nsp_status_t __evfs_cache_read_harddisk(int cluster_id, unsigned char *data, int offset, int length)
{
    nsp_status_t status;
    evfs_cluster_pt clusterptr;

    clusterptr = (evfs_cluster_pt)evfs_hard_allocate_cluster(NULL);
    if (!clusterptr) {
        status = posix__makeerror(ENOMEM);
    } else {
        status = evfs_hard_read_cluster(cluster_id, clusterptr);
        if (NSP_SUCCESS(status)) {
            memcpy(data, (char *)clusterptr + offset, length);
        }
        evfs_hard_release_cluster(clusterptr);
    }
    return status;
}

/* write to harddisk directly */
static nsp_status_t __evfs_cache_write_harddisk(int cluster_id, const unsigned char *data, int offset, int length)
{
    nsp_status_t status;
    evfs_cluster_pt clusterptr;

    clusterptr = (evfs_cluster_pt)evfs_hard_allocate_cluster(NULL);
    if (!clusterptr) {
        status = posix__makeerror(ENOMEM);
    } else {
        status = evfs_hard_read_cluster(cluster_id, clusterptr);
        if (NSP_SUCCESS(status)) {
            memcpy((char *)clusterptr + offset, data, length);
            status = evfs_hard_write_cluster(cluster_id, clusterptr);
        }
        evfs_hard_release_cluster(clusterptr);
    }
    return status;
}

/* read from cache
 * step 1, search the clsuter id in lru index, if found, copy data to user buffer and return successful
 * step 2, try to fetch node from idle list and read from harddisk,and save this node to lru list and return successful
 * step 3, replace the head node of lru list, if the replace node state is dirty, write it to harddisk, and read from harddisk,
 *          move the node to tail lru list and use to save user data in which cluster specify now.
 * step 4, if step 3 failed, return error code */
static nsp_status_t __evfs_cache_read(int cluster_id, unsigned char *data, int offset, int length)
{
    nsp_status_t status;

    status = NSP_STATUS_SUCCESSFUL;

    do {
        /* if cache module have been disabled, we read data from harddisk directly */
        if (0 == __evfs_cache_mgr.cache_cluster_count) {
            status = __evfs_cache_read_harddisk(cluster_id, data, offset, length);
            break;
        }

        /* search the clsuter id in lru index */
        status = __evfs_cache_read_from_lru(cluster_id, data, offset, length);
        if (NSP_SUCCESS(status)) {
            break;
        }

        /* try to fetch node from idle list and read from harddisk */
        status = __evfs_cache_read_from_idle(cluster_id, data, offset, length);
        if (NSP_SUCCESS(status)) {
            break;
        }

        /* replace the head node of lru list, if the replace node state is dirty, write it to harddisk, and read from harddisk */
        status = __evfs_cache_replace_read_from_lru(cluster_id, data, offset, length);
    } while(0);

    return status;
}

/* write to cache
 * step 1, search the clsuter id in lru index, if found, copy user data to node buffer and return successful
 * step 2, try to fetch node from idle list and read from harddisk, copy user data to node buffer, and save this node to lru list and return successful
 * step 3, replace the head node of lru list, if the replace node state is dirty, write it to harddisk, and read from harddisk, cover user data to node buffer,
 *          move the node to tail lru list and use to save user data in which cluster specify now.
 * step 4, if step 3 failed, return error code
 */
static nsp_status_t __evfs_cache_write(int cluster_id, const unsigned char *data, int offset, int length)
{
    nsp_status_t status;

    status = NSP_STATUS_SUCCESSFUL;

    do {
        /* if cache module have been disabled, we write data to harddisk directly */
        if (0 == __evfs_cache_mgr.cache_cluster_count) {
            status = __evfs_cache_write_harddisk(cluster_id, data, offset, length);
            break;
        }

        /* search the clsuter id in lru index */
        status = __evfs_cache_write_to_lru(cluster_id, data, offset, length);
        if (NSP_SUCCESS(status)) {
            break;
        }

        /* try to fetch node from idle list and read from harddisk */
        status = __evfs_cache_write_to_idle(cluster_id, data, offset, length);
        if (NSP_SUCCESS(status)) {
            break;
        }

        /* replace the head node of lru list, if the replace node state is dirty, write it to harddisk, and read from harddisk */
        status = __evfs_cache_replace_write_to_lru(cluster_id, data, offset, length);
    } while(0);

    return status;
}

/* add some blocks and push all of them to idle list */
static int __evfs_cache_add_block_to_idle(int cache_cluster_count)
{
    int i;
    struct evfs_cache_node *node;

    for (i = 0; i < cache_cluster_count; i++) {
        node = (struct evfs_cache_node *)ztrymalloc(sizeof(*node));
        if (!node) {
            break;
        }
        node->cluster_id = 0;
        node->state = kEvfsCacheBlockIdle;
        node->data = (unsigned char *)ztrymalloc(__evfs_cache_mgr.cluster_size);
        if (!node->data) {
            zfree(node);
            break;
        }

        /* insert into idle list */
        list_add_tail(&node->element_of_lru, &__evfs_cache_mgr.idle.head);
        __evfs_cache_mgr.idle.count++;
        __evfs_cache_mgr.cache_cluster_count++;
    }

    __evfs_cache_mgr.cache_cluster_count += i;
    return i;
}

static nsp_status_t __evfs_cache_init_merge_thread()
{
    __evfs_cache_mgr.merge.stop = 0;
    INIT_LIST_HEAD(&__evfs_cache_mgr.merge.io.head);
    lwp_mutex_init(&__evfs_cache_mgr.merge.mutex, 0);
    lwp_event_init(&__evfs_cache_mgr.merge.cond, LWPEC_NOTIFY);
    return lwp_create(&__evfs_cache_mgr.merge.thread, 0, __evfs_cache_thread_merge_io, NULL);
}

nsp_status_t evfs_cache_init(const char *file, int cache_cluster_count, const struct evfs_cache_creator *creator)
{
    int expect;
    nsp_status_t status;

    if (cache_cluster_count < 0) {
        return posix__makeerror(EINVAL);
    }

    expect = kEvmgrNotReady;
    if (!atom_compare_exchange_strong(&__evfs_cache_mgr.ready, &expect, kEvmgrInitializing)) {
        return posix__makeerror(EEXIST);
    }

    status = (NULL != creator) ? evfs_hard_create(file, creator->cluster_size, creator->cluster_count) : evfs_hard_open(file);
    if (!NSP_SUCCESS(status)) {
        atom_set(&__evfs_cache_mgr.ready, kEvmgrNotReady);
        return status;
    }

    do {
        __evfs_cache_mgr.cluster_size = evfs_hard_get_cluster_size();
        if (__evfs_cache_mgr.cluster_size <= 0) {
            status = posix__makeerror(EINVAL);
        }

        /* zero cache count means disable cache module, this is not a error */
        if (0 == cache_cluster_count) {
            status = NSP_STATUS_SUCCESSFUL;
        }

        /* create blocks and push all to idle list */
        if (0 == (__evfs_cache_mgr.cache_cluster_count = __evfs_cache_add_block_to_idle(cache_cluster_count))) {
            status = posix__makeerror(ENOMEM);
        }

        /* create the auto flush thread */
        status = __evfs_cache_init_merge_thread();
    } while(0);

    if (!NSP_SUCCESS(status)) {
        atom_set(&__evfs_cache_mgr.ready, kEvmgrNotReady);
        return status;
    } else {
        atom_set(&__evfs_cache_mgr.ready, kEvmgrReady);
    }

    return status;
}

nsp_status_t evfs_cache_add_block(int cache_cluster_count)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = 0;
    task->offset = 0;
    task->length = cache_cluster_count;
    task->data = NULL;
    task->no_wait = 0;
    task->io_type = kEvfsCacheIOTypeAddCacheBlock;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

void evfs_cache_uninit()
{
    int expect;
    struct evfs_cache_node *node;
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    expect = kEvmgrReady;
    if (!atom_compare_exchange_strong(&__evfs_cache_mgr.ready, &expect, kEvmgrUninitializing)) {
        return;
    }

    /* step 1. push a task to worker thread to close filesystem and wait it complete */
    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (task) {
        task->cluster_id = 0;
        task->offset = 0;
        task->length = 0;
        task->data = NULL;
        task->no_wait = 0;
        task->io_type = kEvfsCacheIOTypeHardClose;
        status = __evfs_cache_push_task_and_notify_run(task, 0);
        if (NSP_SUCCESS(status)) {
            __evfs_cache_wait_background_compelete(task);
        } else {
            zfree(task);
        }
    }

    /* step 2. stop the worker thread */
    lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
    atom_set(&__evfs_cache_mgr.merge.stop, 1);
    lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);

    lwp_event_awaken(&__evfs_cache_mgr.merge.cond);
    lwp_join(&__evfs_cache_mgr.merge.thread, NULL);

    lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
    /* step 3. remove all nodes from lru list and free their memory */
    while (!list_empty(&__evfs_cache_mgr.lru.head)) {
        node = container_of(__evfs_cache_mgr.lru.head.next, struct evfs_cache_node, element_of_lru);
        list_del_init(&node->element_of_lru);
        __evfs_cache_mgr.root_of_lru_index = avlremove(__evfs_cache_mgr.root_of_lru_index, &node->leaf_of_lru_index, NULL, &__evfs_cache_compare_by_cluster_id);
        __evfs_cache_mgr.lru.count--;
        zfree(node->data);
        zfree(node);
    }

    /* step 4. remove all unused idle block in list and free their memory */
    while (!list_empty(&__evfs_cache_mgr.idle.head)) {
        node = container_of(__evfs_cache_mgr.idle.head.next, struct evfs_cache_node, element_of_lru);
        list_del_init(&node->element_of_lru);
        __evfs_cache_mgr.idle.count--;
        zfree(node->data);
        zfree(node);
    }
    lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);

    /* step 5. reset all list head */
    INIT_LIST_HEAD(&__evfs_cache_mgr.idle.head);
    INIT_LIST_HEAD(&__evfs_cache_mgr.lru.head);
    INIT_LIST_HEAD(&__evfs_cache_mgr.dirty.head);

    /* step 6. free all cache manager resources */
    lwp_mutex_uninit(&__evfs_cache_mgr.merge.mutex);
    lwp_event_uninit(&__evfs_cache_mgr.merge.cond);

    /* step 7. reset all other fields in manger */
    __evfs_cache_mgr.cluster_size = 0;
    __evfs_cache_mgr.cache_cluster_count = 0;

    /* step 8. recover the ready states */
    atom_set(&__evfs_cache_mgr.ready, kEvmgrNotReady);
}

/* build a read task and insert it into the merge io list, ask the merge io thread to read the data from harddisk
 * this proc will wait until the merge io thread read the data from harddisk and copy the data to the output buffer */
nsp_status_t evfs_cache_read(int cluster_id, void *output, int offset, int length)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    /* we didn't allow read zero cluster from cache */
    if (cluster_id <= 0 || !output || offset < 0 || length < 0 || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready) || offset + length > __evfs_cache_mgr.cluster_size) {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = cluster_id;
    task->offset = offset;
    task->length = length;
    task->data = output;
    task->io_type = kEvfsCacheIOTypeRead;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

nsp_status_t evfs_cache_read_head(int cluster_id, evfs_cluster_pt clusterptr)
{
    return evfs_cache_read(cluster_id, clusterptr, 0, sizeof(*clusterptr));
}

nsp_status_t evfs_cache_read_userdata(int cluster_id, void *output, int offset, int length)
{
    if (!output || offset + length > __evfs_cache_mgr.cluster_size - sizeof(struct evfs_cluster) || cluster_id < 0) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_read(cluster_id, output, offset + sizeof(struct evfs_cluster), length);
}

/* build a directly disk read task and insert it into the merge io list, ask the merge io thread to read the data from harddisk
 * this proc will wait until the merge io thread read the data from harddisk and copy the data to the output buffer */
nsp_status_t evfs_cache_read_directly(int cluster_id, void *output)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (cluster_id <= 0 || !output || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = cluster_id;
    task->offset = 0;
    task->length = 0;
    task->data = output;
    task->io_type = kEvfsCacheIOTypeReadDirectly;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

nsp_status_t evfs_cache_read_head_directly(int cluster_id, evfs_cluster_pt clusterptr)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (cluster_id <= 0 || !clusterptr || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = cluster_id;
    task->offset = 0;
    task->length = 0;
    task->ptr = clusterptr;
    task->io_type = kEvfsCacheIOTypeReadHeadDirectly;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

/* build a write task and insert it into the merge io list, ask the merge io thread to write the data to cache node
 * this proc will wait until the merge io thread write the data to cache node */
nsp_status_t evfs_cache_write(int cluster_id, const void *input, int offset, int length)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    /* we didn't allow write zero cluster to cache */
    if (cluster_id <= 0 || !input || offset < 0 || length < 0 ||
        offset + length > __evfs_cache_mgr.cluster_size - sizeof(struct evfs_cluster) || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready))
    {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = cluster_id;
    task->offset = offset;
    task->length = length;
    task->cdata = input;
    task->io_type = kEvfsCacheIOTypeWrite;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

nsp_status_t evfs_cache_write_head(int cluster_id, const evfs_cluster_pt clusterptr)
{
    return evfs_cache_write(cluster_id, (const unsigned char *)clusterptr, 0, sizeof(*clusterptr));
}

nsp_status_t evfs_cache_write_userdata(int cluster_id, const void *input, int offset, int length)
{
    if (!input || (offset + length) > (__evfs_cache_mgr.cluster_size - sizeof(struct evfs_cluster)) || cluster_id < 0) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_write(cluster_id, input, offset + sizeof(struct evfs_cluster), length);
}

nsp_status_t evfs_cache_write_directly(int cluster_id, const void *input)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (cluster_id <= 0 || !input || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return posix__makeerror(EINVAL);
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return posix__makeerror(ENOMEM);
    }
    task->cluster_id = cluster_id;
    task->offset = 0;
    task->length = 0;
    task->cdata = input;
    task->io_type = kEvfsCacheIOTypeWriteDirectly;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, 0);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return status;
    }

    /* wait for the io thread to finish the task */
    return __evfs_cache_wait_background_compelete(task);
}

/* flush all dirty cache blocks to harddisk */
static void __evfs_cache_flush_buffer_all()
{
    struct evfs_cache_node *node;
    struct list_head *pos, *n;

    if (0 == __evfs_cache_mgr.dirty.count) {
        return;
    }

    list_for_each_safe(pos, n, &__evfs_cache_mgr.dirty.head) {
        node = container_of(pos, struct evfs_cache_node, element_of_dirty);
        __evfs_cache_flush_block_if_dirty(node);
    }
}

void evfs_cache_flush(int no_wait)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return;
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return;
    }
    task->cluster_id = 0;
    task->offset = 0;
    task->length = 0;
    task->data = NULL;
    task->io_type = kEvfsCacheIOTypeFlushBufferAll;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, no_wait);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return;
    }

    /* wait for the io thread to finish the task */
    __evfs_cache_wait_background_compelete(task);
}

void __evfs_cache_flush_block(int cluster_id)
{
    struct evfs_cache_node *node;

    node = __evfs_cache_search_lru(cluster_id);
    if (node) {
        __evfs_cache_flush_block_if_dirty(node);
    }
}

void evfs_cache_flush_block(int cluster_id, int no_wait)
{
    struct evfs_cache_io_task *task;
    nsp_status_t status;

    if (cluster_id <= 0 || kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return;
    }

    task = (struct evfs_cache_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return;
    }
    task->cluster_id = cluster_id;
    task->offset = 0;
    task->length = 0;
    task->data = NULL;
    task->io_type = kEvfsCacheIOTypeFlushBuffer;

    /* push task to io list and awaken the io thread */
    status = __evfs_cache_push_task_and_notify_run(task, no_wait);
    if (!NSP_SUCCESS(status)) {
        zfree(task);
        return;
    }

    /* wait for the io thread to finish the task */
    __evfs_cache_wait_background_compelete(task);
}

float evfs_cache_hit_rate()
{
    int count_of_hit;
    int count_of_miss;

    if (kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return 0.0f;
    }

    count_of_hit = atom_get(&__evfs_cache_mgr.statistics.count_of_hit);
    count_of_miss = atom_get(&__evfs_cache_mgr.statistics.count_of_miss);

    if (count_of_hit + count_of_miss == 0) {
        return 0.0f;
    }

    return (float)count_of_hit / ((float)count_of_hit +  count_of_miss);
}

nsp_status_t evfs_cache_hard_state(struct evfs_cache_creator *creator)
{
    if (!creator) {
        return posix__makeerror(EINVAL);
    }

    if (kEvmgrReady != atom_get(&__evfs_cache_mgr.ready)) {
        return posix__makeerror(ENODEV);
    }

    creator->cluster_count = evfs_hard_get_usable_cluster_count();
    creator->cluster_size = evfs_hard_get_cluster_size();
    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t __evfs_cache_push_task_and_notify_run(struct evfs_cache_io_task *task, int no_wait)
{
    nsp_status_t status;

    task->no_wait = no_wait;
    
    lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
    do {
        if (__evfs_cache_mgr.merge.stop) {
            status = posix__makeerror(ENODEV);
            break;
        }
        if (__evfs_cache_mgr.merge.io.count >= EVFS_MAX_IO_PENDING_COUNT) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        list_add_tail(&task->element, &__evfs_cache_mgr.merge.io.head);
        __evfs_cache_mgr.merge.io.count++;
    } while(0);
    lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);

    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (!no_wait) {
        lwp_event_init(&task->cond, LWPEC_NOTIFY);
    }
    lwp_event_awaken(&__evfs_cache_mgr.merge.cond);

    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t __evfs_cache_wait_background_compelete(struct evfs_cache_io_task *task)
{
    nsp_status_t status;

    if (task->no_wait) {
        return NSP_STATUS_SUCCESSFUL;
    }

    lwp_event_wait(&task->cond, -1);
    status = task->status;
    lwp_event_uninit(&task->cond);
    zfree(task);

    return status;
}

/* flush all dirty blocks to harddisk by following rule(Imitate the 'Redis' opensource program):
    equal to or large than 10000 updates in 60 seconds,
    equal to or large than 10 updates in 300 seconds,
    equal to or large than 1 updates in 900 seconds  */
static int __evfs_cache_auto_flush(uint64_t previous_flust_timestamp, int writtens)
{
    uint64_t now;
    static const uint64_t FLUSH_TIME_PERIOD_10000 = (uint64_t)60 * 1000 * 1000 * 10;
    static const uint64_t FLUSH_TIME_PERIOD_10 = (uint64_t)300 * 1000 * 1000 * 10;
    static const uint64_t FLUSH_TIME_PERIOD_1 = (uint64_t)900 * 1000 * 1000 * 10;

    now = clock_monotonic();
    if ((writtens >= 10000 && now - previous_flust_timestamp >= FLUSH_TIME_PERIOD_10000) ||
        (writtens >= 10 && now - previous_flust_timestamp >= FLUSH_TIME_PERIOD_10) ||
        (writtens >= 1 && now - previous_flust_timestamp >= FLUSH_TIME_PERIOD_1))
    {
        __evfs_cache_flush_buffer_all();
        previous_flust_timestamp = clock_monotonic();
        return 0;
    }

    return writtens;
}

/* execute task */
static void __evfs_cache_exec_task(struct evfs_cache_io_task *task)
{
    switch(task->io_type) {
    case kEvfsCacheIOTypeRead:
        task->status = __evfs_cache_read(task->cluster_id, task->data, task->offset, task->length);
        break;
    case kEvfsCacheIOTypeReadDirectly:
        task->status = evfs_hard_read_cluster(task->cluster_id, task->data);
        break;
    case kEvfsCacheIOTypeReadHeadDirectly:
        task->status = evfs_hard_read_cluster_head(task->cluster_id, (struct evfs_cluster *)task->data);
        break;
    case kEvfsCacheIOTypeWrite:
        task->status = __evfs_cache_write(task->cluster_id, task->data, task->offset, task->length);
        break;
    case kEvfsCacheIOTypeWriteDirectly:
        task->status = evfs_hard_write_cluster(task->cluster_id, task->data);
        break;
    case kEvfsCacheIOTypeFlushBufferAll:
        __evfs_cache_flush_buffer_all();
        task->status = NSP_STATUS_SUCCESSFUL;
        break;
    case kEvfsCacheIOTypeFlushBuffer:
        __evfs_cache_flush_block(task->cluster_id);
        task->status = NSP_STATUS_SUCCESSFUL;
        break;
    case kEvfsCacheIOTypeAddCacheBlock:
        task->status = (0 == __evfs_cache_add_block_to_idle(task->length)) ?
            posix__makeerror(ENOMEM) : NSP_STATUS_SUCCESSFUL;
        break;
    case kEvfsCacheIOTypeHardClose:
        evfs_hard_close();
        task->status = NSP_STATUS_SUCCESSFUL;
        break;
    default:
        task->status = posix__makeerror(EINVAL);
        break;
    }

    if (!task->no_wait) {
        lwp_event_awaken(&task->cond);
    } else{
        zfree(task);
    }
}

static void __evfs_cache_pop_task_until_empty()
{
    struct evfs_cache_io_task *task;

    lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
    while (NULL != (task = list_first_entry_or_null(&__evfs_cache_mgr.merge.io.head, struct evfs_cache_io_task, element))) {
        list_del_init(&task->element);
        __evfs_cache_mgr.merge.io.count--;
        lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);

        __evfs_cache_exec_task(task);

        lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
    }
    lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);
}

static void *__evfs_cache_thread_merge_io(void *parameter)
{
    nsp_status_t status;
    static const int expire = 5 * 1000;
    int stop;

    while (1) {
        lwp_mutex_lock(&__evfs_cache_mgr.merge.mutex);
        stop = __evfs_cache_mgr.merge.stop;
        lwp_mutex_unlock(&__evfs_cache_mgr.merge.mutex);
        if (stop) {
            break;
        }
        
        status = lwp_event_wait(&__evfs_cache_mgr.merge.cond, expire);
        if (!NSP_SUCCESS(status)) {
            /* no any IO request happend during 5 seconds, flush all dirty block to harddisk  */
            if (NSP_FAILED_AND_ERROR_EQUAL(status, ETIMEDOUT)) {
                __evfs_cache_flush_buffer_all();
                continue;
            } else {
                break;
            }
        }
        lwp_event_block(&__evfs_cache_mgr.merge.cond);
        
        __evfs_cache_pop_task_until_empty();
    }

    __evfs_cache_pop_task_until_empty();
    lwp_event_uninit(&__evfs_cache_mgr.merge.cond);
    return NULL;
}
