#include "evfs.h"

#include "cache.h"
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

enum evfs_io_task_type {
    kEvfsIoTaskRead = 0,
    kEvfsIoTaskWrite,
    kEvfsIoTaskTruncate,
    kEvfsIoTaskFlush,
    kEvfsIoTaskFlushAll,
    kEvfsIoTaskErase,
    kEvfsIoTaskEraseByKey,
    kEvfsIoTaskCreate,
    kEvfsIoTaskSetCache,
    kEvfsIoTaskMaximumFunction,
};

struct evfs_io_task {
    nsp_status_t status;
    int entry_id;
    int offset;
    int length;
    union {
        char *data;
        const char *cdata;
        void *ptr;
    };
    int type;
    struct list_head element_of_task_list;
    int retval;
    lwp_event_t cond;
    int no_wait;
    struct evfs_entry_descriptor *descriptor;
};

static void __evfs_write_entry(struct evfs_io_task *task);
static void __evfs_read_entry(struct evfs_io_task *task);
static void __evfs_earse_entry_by_key(struct evfs_io_task *task);
static void __evfs_earse_entry(struct evfs_io_task *task);
static void __evfs_flush_entry_buffer(struct evfs_io_task *task);
static void __evfs_flush_all(struct evfs_io_task *task);
static void __evfs_truncate_entry(struct evfs_io_task *task);
static void __evfs_create_entry(struct evfs_io_task *task);

static const struct {
    int io_task_type;
    void (*io_task_handler)(struct evfs_io_task *task);
} __evfs_io_task_exec[kEvfsIoTaskMaximumFunction] = {
    { .io_task_type = kEvfsIoTaskRead,         .io_task_handler = __evfs_read_entry }, 
    { .io_task_type = kEvfsIoTaskWrite,        .io_task_handler = __evfs_write_entry }, 
    { .io_task_type = kEvfsIoTaskTruncate,     .io_task_handler = __evfs_truncate_entry }, 
    { .io_task_type = kEvfsIoTaskFlush,        .io_task_handler = __evfs_flush_entry_buffer }, 
    { .io_task_type = kEvfsIoTaskFlushAll,     .io_task_handler = __evfs_read_entry }, 
    { .io_task_type = kEvfsIoTaskErase,        .io_task_handler = __evfs_earse_entry }, 
    { .io_task_type = kEvfsIoTaskEraseByKey,   .io_task_handler = __evfs_earse_entry_by_key }, 
    { .io_task_type = kEvfsIoTaskCreate,       .io_task_handler = __evfs_create_entry }, 
    { .io_task_type = kEvfsIoTaskSetCache,     .io_task_handler = __evfs_read_entry }, 
};


struct evfs_io_background_worker {
    int stop;
    lwp_t thread;
    lwp_mutex_t mutex;
    lwp_event_t cond;
    struct list_head head_of_tasks;
    int count_of_tasks;
};

#if !defined EVFS_MAX_WORKER
#define EVFS_MAX_WORKER (4)
#endif

#if !defined EVFS_MAX_IO_PENDING_COUNT
#define EVFS_MAX_IO_PENDING_COUNT (200)
#endif

static struct {
    int ready;
    int next_handle;
    struct avltree_node_t *root_of_descriptors;
    struct list_head head_of_descriptors;
    int count_of_descriptors;
    lwp_mutex_t mutex;
    struct evfs_io_background_worker worker[EVFS_MAX_WORKER];
} __evfs_descriptor_mgr = {
    .ready = kEvmgrNotReady,
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

static void __evfs_close_descriptor(struct evfs_entry_descriptor *descriptor)
{
    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    if ( kEvfsDescriptorActived == descriptor->stat) {
        descriptor->stat = kEvfsDescriptorCloseWait;
    }
    if (0 == descriptor->refcnt) {
        __evfs_dequeue_descriptor(descriptor);
        zfree(descriptor);
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

static void __evfs_pop_task_until_empty(struct evfs_io_background_worker *worker)
{
    struct evfs_io_task *task;

    lwp_mutex_lock(&worker->mutex);
    while (NULL != (task = list_first_entry_or_null(&worker->head_of_tasks, struct evfs_io_task, element_of_task_list))) {
        list_del_init(&task->element_of_task_list);
        worker->count_of_tasks--;
        lwp_mutex_unlock(&worker->mutex);

        __evfs_io_task_exec[task->type].io_task_handler(task);
        if (!task->no_wait) {
            lwp_event_awaken(&task->cond);
        } else{
            zfree(task);
        }

        lwp_mutex_lock(&worker->mutex);
    }
    lwp_mutex_unlock(&worker->mutex);
}

static void *__evfs_worker_proc(void *p)
{
    struct evfs_io_background_worker *worker = (struct evfs_io_background_worker *)p;
    nsp_status_t status;
    int stop;

    while (1) {
        lwp_mutex_lock(&worker->mutex);
        stop = worker->stop;
        lwp_mutex_unlock(&worker->mutex);
        if (stop) {
            break;
        }
        
        status = lwp_event_wait(&worker->cond, -1);
        if (!NSP_SUCCESS(status)) {
            break;
        }
        lwp_event_block(&worker->cond);
        
        __evfs_pop_task_until_empty(worker);
    }

    __evfs_pop_task_until_empty(worker);
    lwp_event_uninit(&worker->cond);
    return NULL;
}

static nsp_status_t __evfs_io_push_task_and_notify_run(struct evfs_io_background_worker *worker, struct evfs_io_task *task)
{
    nsp_status_t status;

    status = NSP_STATUS_SUCCESSFUL;
    
    lwp_mutex_lock(&worker->mutex);
    do {
        if (worker->stop) {
            status = posix__makeerror(ENODEV);
            break;
        }
        if (worker->count_of_tasks >= EVFS_MAX_IO_PENDING_COUNT) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        list_add_tail(&task->element_of_task_list, &worker->head_of_tasks);
        worker->count_of_tasks++;
    } while(0);
    lwp_mutex_unlock(&worker->mutex);

    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (!task->no_wait) {
        lwp_event_init(&task->cond, LWPEC_NOTIFY);
    }
    lwp_event_awaken(&worker->cond);

    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t __evfs_io_wait_background_compelete(struct evfs_io_task *task, int *retval)
{
    nsp_status_t status;

    if (task->no_wait) {
        return NSP_STATUS_SUCCESSFUL;
    }

    lwp_event_wait(&task->cond, -1);
    status = task->status;
    lwp_event_uninit(&task->cond);
    if (retval) {
        *retval = task->retval;
    }
    zfree(task);

    return status;
}

nsp_status_t evfs_create(const char *path, int cluster_size_format, int cluster_count_format, int cache_block_num)
{
    nsp_status_t status;
    int expect;
    struct evfs_cache_creator creator;
    int i;

    if (!path || cluster_size_format <= 0 || cluster_count_format <= 0) {
        return posix__makeerror(EINVAL);
    }

    expect = kEvmgrNotReady;
    if (!atom_compare_exchange_strong(&__evfs_descriptor_mgr.ready, &expect, kEvmgrInitializing)) {
        return posix__makeerror(EEXIST);
    }

    creator.hard_cluster_size = cluster_size_format;
    creator.hard_cluster_count = cluster_count_format;
    status = evfs_cache_init(path, cache_block_num, &creator);
    if (!NSP_SUCCESS(status)) {
        atom_set(&__evfs_descriptor_mgr.ready, kEvmgrNotReady);
        return status;
    }

    status = evfs_entries_create();
    if (!NSP_SUCCESS(status)) {
        evfs_cache_uninit();
        atom_set(&__evfs_descriptor_mgr.ready, kEvmgrNotReady);
        return status;
    }

    lwp_mutex_init(&__evfs_descriptor_mgr.mutex, 1);
    atom_set(&__evfs_descriptor_mgr.next_handle, 0);

    /* create worker thread and initial task queue */
    for (i = 0; i < EVFS_MAX_WORKER; i++) {
        __evfs_descriptor_mgr.worker[i].stop = 0;
        lwp_mutex_init(&__evfs_descriptor_mgr.worker[i].mutex, 0);
        lwp_event_init(&__evfs_descriptor_mgr.worker[i].cond, LWPEC_NOTIFY);
        INIT_LIST_HEAD(&__evfs_descriptor_mgr.worker[i].head_of_tasks);
        __evfs_descriptor_mgr.worker[i].count_of_tasks = 0;
        lwp_create(&__evfs_descriptor_mgr.worker[i].thread, 0, __evfs_worker_proc, &__evfs_descriptor_mgr.worker[i]);
    }

    atom_set(&__evfs_descriptor_mgr.ready, kEvmgrReady);
    return status;
}

nsp_status_t evfs_open(const char *path, int cache_block_num)
{
    nsp_status_t status;
    int expect;
    int i;

    if (!path) {
        return posix__makeerror(EINVAL);
    }

    expect = kEvmgrNotReady;
    if (!atom_compare_exchange_strong(&__evfs_descriptor_mgr.ready, &expect, kEvmgrInitializing)) {
        return posix__makeerror(EEXIST);
    }

    status = evfs_cache_init(path, cache_block_num, NULL);
    if (!NSP_SUCCESS(status)) {
        atom_set(&__evfs_descriptor_mgr.ready, kEvmgrNotReady);
        return status;
    }

    status = evfs_entries_load();
    if (!NSP_SUCCESS(status)) {
        evfs_cache_uninit();
        atom_set(&__evfs_descriptor_mgr.ready, kEvmgrNotReady);
        return status;
    } 

    /* create descriptor mutex */
    lwp_mutex_init(&__evfs_descriptor_mgr.mutex, 1);
    atom_set(&__evfs_descriptor_mgr.next_handle, 0);

    /* create worker thread and initial task queue */
    for (i = 0; i < EVFS_MAX_WORKER; i++) {
        __evfs_descriptor_mgr.worker[i].stop = 0;
        lwp_mutex_init(&__evfs_descriptor_mgr.worker[i].mutex, 0);
        lwp_event_init(&__evfs_descriptor_mgr.worker[i].cond, LWPEC_NOTIFY);
        INIT_LIST_HEAD(&__evfs_descriptor_mgr.worker[i].head_of_tasks);
        __evfs_descriptor_mgr.worker[i].count_of_tasks = 0;
        lwp_create(&__evfs_descriptor_mgr.worker[i].thread, 0, __evfs_worker_proc, &__evfs_descriptor_mgr.worker[i]);
    }
    
    atom_set(&__evfs_descriptor_mgr.ready, kEvmgrReady);
    return status;
}

void evfs_close()
{
    struct evfs_entry_descriptor *descriptor;
    int expect;

    expect = kEvmgrReady;
    if (!atom_compare_exchange_strong(&__evfs_descriptor_mgr.ready, &expect, kEvmgrUninitializing)) {
        return;
    }

    /* stop all threads and clear task list of every worker */
    for (int i = 0; i < EVFS_MAX_WORKER; i++) {
        __evfs_descriptor_mgr.worker[i].stop = 1;
        lwp_event_awaken(&__evfs_descriptor_mgr.worker[i].cond);
        lwp_join(&__evfs_descriptor_mgr.worker[i].thread, NULL);
        lwp_event_uninit(&__evfs_descriptor_mgr.worker[i].cond);
        lwp_mutex_uninit(&__evfs_descriptor_mgr.worker[i].mutex);
    }

    /* destroy all descriptors */
    lwp_mutex_lock(&__evfs_descriptor_mgr.mutex);
    while (NULL != (descriptor = list_first_entry_or_null(&__evfs_descriptor_mgr.head_of_descriptors, struct evfs_entry_descriptor, element_of_descriptor_list))) {
        __evfs_close_descriptor(descriptor);
    }
    lwp_mutex_unlock(&__evfs_descriptor_mgr.mutex);

    evfs_entries_uninit();
    evfs_cache_uninit();

    atom_set(&__evfs_descriptor_mgr.next_handle, -1);
    atom_set(&__evfs_descriptor_mgr.ready, kEvmgrNotReady);
}

static void __evfs_flush_all(struct evfs_io_task *task) 
{
    evfs_cache_flush(1);
}

void evfs_flush()
{
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if ( atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return;
    }

    worker = &__evfs_descriptor_mgr.worker[0];

    /* alloc a task, the cache direct request post to worker[0] */
    task = (struct evfs_io_task *)ztrymalloc(sizeof(*task));
    if (!task) {
        return;
    }
    task->ptr = NULL;
    task->type = kEvfsIoTaskFlushAll;
    task->offset = 0;
    task->length = 0;
    task->descriptor = NULL;
    task->entry_id = 0;
    task->status = 0;
    task->no_wait = 0;
    task->retval = 0;
    __evfs_io_push_task_and_notify_run(worker, task);
}

nsp_status_t evfs_query_stat(evfs_stat_t *evstat)
{
    struct evfs_cache_stat hard;
    struct evfs_entries_stat soft;
    nsp_status_t status;

    if (!evstat || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    /* hard stat */
    status = evfs_cache_hard_state(&hard);
    if (!NSP_SUCCESS(status)) {
        return status;
    }
    evstat->file_size = hard.file_size;
    evstat->cluster_count = hard.hard_cluster_count;
    evstat->cluster_size = hard.hard_cluster_size;
    evstat->cache_block_num = hard.cache_block_num;

    /* soft state */
    status = evfs_entries_soft_stat(&soft);
    if (!NSP_SUCCESS(status)) {
        return status;
    }
    evstat->entry_count = soft.total_entry_count;
    evstat->cluster_busy = soft.busy_view_count;
    evstat->cluster_idle = soft.idle_view_count;

    /* cache hit rate */
    evstat->cache_hit_rate = evfs_cache_hit_rate();

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t evfs_set_cache_block_num(int cache_block_num)
{
    if (atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_set_block_num(cache_block_num);
}

static void __evfs_create_entry(struct evfs_io_task *task)
{
    struct evfs_entry_descriptor *descriptor;

    descriptor = task->descriptor;
    if (descriptor) {
        task->status = evfs_entries_create_one(task->cdata, &descriptor->entry_id);
    } else {
        task->status = evfs_entries_create_one(task->cdata, &task->entry_id);
    }
}

evfs_entry_handle_t evfs_create_entry(const char *key)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if (!key || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    if (!__evfs_is_legal_key(key)) {
        return posix__makeerror(EINVAL);
    }

    task = NULL;
    descriptor = NULL;
    do {
        descriptor = (struct evfs_entry_descriptor *)ztrycalloc(sizeof(*descriptor));
        if (!descriptor) {
            break;
        }
        memset(descriptor, 0, sizeof(*descriptor));

        task = (struct evfs_io_task *)ztrymalloc(sizeof(*task));
        if (!task) {
            break;
        }

        memset(task, 0, sizeof(*task));
        task->cdata = key;
        task->type = kEvfsIoTaskCreate;
        task->offset = 0;
        task->length = 0;
        task->descriptor = NULL;
        task->entry_id = 0;
        task->status = 0;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[0];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        status = __evfs_io_wait_background_compelete(task, NULL);
        task = NULL;

        if (NSP_SUCCESS(status)) {
            descriptor->handle = atom_addone(&__evfs_descriptor_mgr.next_handle);
            descriptor->offset = 0;
            descriptor->stat = kEvfsDescriptorActived;
            /* queue the descriptor */
            __evfs_queue_descriptor(descriptor);
            return descriptor->handle;
        }
    } while (0);

    if (task) {
        zfree(task);
    }
    if (descriptor) {
        zfree(descriptor);
    }
    return -1;
}

evfs_entry_handle_t evfs_open_entry(int entry_id)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;

    if (entry_id < 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
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
    descriptor->handle = atom_addone(&__evfs_descriptor_mgr.next_handle);
    descriptor->entry_id = entry_id;
    descriptor->stat = kEvfsDescriptorActived;
    
    /* queue the descriptor */
    __evfs_queue_descriptor(descriptor);

    return descriptor->handle;
}

evfs_entry_handle_t evfs_open_entry_by_key(const char *key)
{
    nsp_status_t status;
    struct evfs_entry_descriptor *descriptor;

    if (!key || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
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
    descriptor->handle = atom_addone(&__evfs_descriptor_mgr.next_handle);
    descriptor->stat = kEvfsDescriptorActived;

    /* queue the descriptor */
    __evfs_queue_descriptor(descriptor);

    return descriptor->handle;
}

int evfs_get_entry_size(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    int data_seg_size;

    if (handle < 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
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

    if (handle < 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return;
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return;
    }

    if (descriptor->entry_id > 0) {
        evfs_entries_close_one(descriptor->entry_id);
    }
    
    __evfs_dereference_descriptor(descriptor);
    __evfs_close_descriptor(descriptor);
}

static void __evfs_write_entry(struct evfs_io_task *task)
{
    int cpsize;
    struct evfs_entry_descriptor *descriptor;

    descriptor = task->descriptor;

    if (descriptor) {
        cpsize = evfs_entries_write_data(descriptor->entry_id, task->cdata, descriptor->offset, task->length);
        if (cpsize > 0) {
            descriptor->offset += cpsize;
        }
    } else {
        cpsize = evfs_entries_write_data(task->entry_id, task->cdata, 0, task->length);
    }

    task->status = NSP_STATUS_SUCCESSFUL;
    task->retval = cpsize;
}

int evfs_write_entry(evfs_entry_handle_t handle, const char *data, int size)
{   
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;
    nsp_status_t status;
    int cpsize;
    struct evfs_entry_descriptor *descriptor;

    if ( handle <= 0 || !data || size <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    cpsize = 0;
    task = NULL;
    do {
        descriptor = __evfs_reference_descriptor(handle);
        if (!descriptor) {
            break;
        }

        /* alloc io task */
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            return posix__makeerror(ENOMEM);
        }
        task->type = kEvfsIoTaskWrite;
        task->cdata = data;
        task->entry_id = descriptor->entry_id;
        task->length = size;
        task->offset = descriptor->offset;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[descriptor->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, &cpsize);
        task = NULL;
    } while(0);

    __evfs_dereference_descriptor(descriptor);
    return cpsize;
}

static void __evfs_read_entry(struct evfs_io_task *task)
{
    int cpsize;
    struct evfs_entry_descriptor *descriptor;

    descriptor = task->descriptor;

    if (descriptor) {
        cpsize = evfs_entries_read_data(descriptor->entry_id, task->data, descriptor->offset, task->length);
        if (cpsize > 0) {
            descriptor->offset += cpsize;
        }
    } else {
        cpsize = evfs_entries_read_data(task->entry_id, task->data, task->offset, task->length);
    }

    task->status = NSP_STATUS_SUCCESSFUL;
    task->retval = cpsize;
}

int evfs_read_entry(evfs_entry_handle_t handle, char *data, int size)
{
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;
    nsp_status_t status;
    int cpsize;
    struct evfs_entry_descriptor *descriptor;

    if ( handle <= 0 || !data || size <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    cpsize = 0;
    task = NULL;
    do {
        descriptor = __evfs_reference_descriptor(handle);
        if (!descriptor) {
            break;
        }

        /* alloc io task */
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            return posix__makeerror(ENOMEM);
        }
        task->type = kEvfsIoTaskRead;
        task->data = data;
        task->entry_id = descriptor->entry_id;
        task->length = size;
        task->offset = descriptor->offset;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[descriptor->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, &cpsize);
        task = NULL;
    } while(0);

    __evfs_dereference_descriptor(descriptor);
    return cpsize;
}

int evfs_read_iterator_entry(const evfs_iterator_pt iterator, char *data, int size)
{
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;
    int cpsize;
    nsp_status_t status;

    if (!iterator || !data || size <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return 0;
    }

    if (iterator->magic != EVFS_ITERATOR_MAGIC) {
        return 0;
    }

    cpsize = 0;
    task = NULL;
    do {
        /* alloc io task */
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            return posix__makeerror(ENOMEM);
        }
        task->type = kEvfsIoTaskRead;
        task->data = data;
        task->entry_id = iterator->entry_id;
        task->length = size;
        task->offset = 0;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = NULL;

        worker = &__evfs_descriptor_mgr.worker[iterator->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, &cpsize);
        task = NULL;
    } while(0);
    return cpsize;
}

int evfs_query_entry_length(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    int length;

    if (handle <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

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

    if (handle <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady || seek < 0) {
        return posix__makeerror(EINVAL);
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }
    
    descriptor->offset = seek;
    __evfs_dereference_descriptor(descriptor);
    return NSP_STATUS_SUCCESSFUL;
}

static void __evfs_truncate_entry(struct evfs_io_task *task)
{
    struct evfs_entry_descriptor *descriptor;
    
    descriptor = task->descriptor;

    if (descriptor) {
        task->status = evfs_entries_truncate(descriptor->entry_id, task->length + MAX_ENTRY_NAME_LENGTH);
    } else {
        task->status = evfs_entries_truncate(task->entry_id, task->length + MAX_ENTRY_NAME_LENGTH);
    }
}

nsp_status_t evfs_truncate_entry(evfs_entry_handle_t handle, int size)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if (handle <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady || size < 0) {
        return posix__makeerror(EINVAL);
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    status = NSP_STATUS_SUCCESSFUL;
    do {
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        task->type = kEvfsIoTaskTruncate;
        task->data = NULL;
        task->entry_id = descriptor->entry_id;
        task->length = size;
        task->offset = 0;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[task->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, NULL);
        task = NULL;
    } while(0);

    __evfs_dereference_descriptor(descriptor);
    return status;
}

static void __evfs_flush_entry_buffer(struct evfs_io_task *task)
{
    struct evfs_entry_descriptor *descriptor;

    descriptor = task->descriptor;
    if (!descriptor) {
        evfs_entries_flush(task->entry_id);
    } else {
        evfs_entries_flush(descriptor->entry_id);
    }

    task->status = NSP_STATUS_SUCCESSFUL;
}

void evfs_flush_entry_buffer(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if (handle <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return;
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return;
    }

    status = NSP_STATUS_SUCCESSFUL;
    do {
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        task->type = kEvfsIoTaskFlush;
        task->data = NULL;
        task->entry_id = descriptor->entry_id;
        task->length = 0;
        task->offset = 0;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[task->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, NULL);
        task = NULL;
    } while(0);

    __evfs_dereference_descriptor(descriptor);
    return;
}

static void __evfs_earse_entry(struct evfs_io_task *task)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;

    descriptor = task->descriptor;
    if (!descriptor) {
        if (task->entry_id > 0) {
            status = evfs_entries_hard_delete(task->entry_id);
        } else {
            status = posix__makeerror(ENOENT);
        }
    } else {
        status = evfs_entries_hard_delete(descriptor->entry_id);
    }

    task->status = status;
}

nsp_status_t evfs_earse_entry(evfs_entry_handle_t handle)
{
    struct evfs_entry_descriptor *descriptor;
    nsp_status_t status;
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if (handle <= 0 || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    descriptor = __evfs_reference_descriptor(handle);
    if (!descriptor) {
        return posix__makeerror(ENOENT);
    }

    status = NSP_STATUS_SUCCESSFUL;
    do {
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        task->type = kEvfsIoTaskErase;
        task->data = NULL;
        task->entry_id = descriptor->entry_id;
        task->length = 0;
        task->offset = 0;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = descriptor;

        worker = &__evfs_descriptor_mgr.worker[task->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            zfree(task);
            task = NULL;
            break;
        }

        status = __evfs_io_wait_background_compelete(task, NULL);
        task = NULL;
    } while(0);

    __evfs_dereference_descriptor(descriptor);
    return status;
}

static void __evfs_earse_entry_by_key(struct evfs_io_task *task)
{
    task->status = evfs_entries_hard_delete_by_name(task->cdata);
}

nsp_status_t evfs_earse_entry_by_key(const char *name)
{
    nsp_status_t status;
    struct evfs_io_task *task;
    struct evfs_io_background_worker *worker;

    if (!name || atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return posix__makeerror(EINVAL);
    }

    task = NULL;
    do {
        /* alloc io task */
        task = (struct evfs_io_task *)ztrycalloc(sizeof(*task));
        if (!task) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        task->type = kEvfsIoTaskEraseByKey;
        task->data = NULL;
        task->entry_id = evfs_entries_get_entry_id_by_name(name);
        if (task->entry_id <= 0) {
            status = posix__makeerror(ENOENT);
            break;
        }
        task->length = 0;
        task->offset = 0;
        task->status = EINPROGRESS;
        task->no_wait = 0;
        task->retval = 0;
        task->descriptor = NULL;
        task->cdata = (char *)name;

        worker = &__evfs_descriptor_mgr.worker[task->entry_id % EVFS_MAX_WORKER];

        status = __evfs_io_push_task_and_notify_run(worker, task);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        status = __evfs_io_wait_background_compelete(task, NULL);
        task = NULL;
    } while(0);

    if (task) {
        zfree(task);
    }
    
    return status;
}

evfs_iterator_pt evfs_iterate_entries(evfs_iterator_pt iterator)
{
    evfs_iterator_pt iter;

    if (atom_get(&__evfs_descriptor_mgr.ready) != kEvmgrReady) {
        return NULL;
    }

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

