#include "entries.h"

#include "view.h"

#include "zmalloc.h"
#include "atom.h"
#include "threading.h"
#include "avltree.h"

#include <string.h>
#include <assert.h>

struct evfs_entry_head;

enum evfs_entry_state
{
    kEvfsEntryNormal = 0,
    kEvfsEntryCloseWait,
    kEvfsEntryBusy,
    kEvfsEntryOpen,
    kEvfsEntryMandatory,
};

struct evfs_entry_element 
{
    int viewid;
    evfs_view_pt view;
    struct evfs_entry_head *entry_head;
    struct list_head element_of_head_entry;  /* [evfs_entry_head::head_of_entry_elements] */
};

struct evfs_entry_head
{
    int viewid;
    evfs_view_pt view;
    struct list_head head_of_entry_elements;  /* struct evfs_entry_element[] */
    int count_of_elements;
    struct avltree_node_t leaf_of_entries_tree;     
    struct avltree_node_t leaf_of_named_entries_tree;
    struct list_head element_of_entries_list;    /* [__evfs_entrirs_mgr::head_of_entries_list] */
    int refcnt;
    int io_refcnt;
    enum evfs_entry_state state;
    int hard_remove_on_detach;      /* remove entry data from harddisk when entry have been detach from manager */
    lwp_mutex_t mutex;
    char key[MAX_ENTRY_NAME_LENGTH];
};

static struct {
     struct avltree_node_t *root_of_entries_tree;  /* map<struct evfs_entry_head> */
     struct avltree_node_t *root_of_named_entries_tree;  /* entries which have explicit name */
     struct list_head head_of_entries_list;  /* struct evfs_entry_head[] */
     int count_of_entries;
     struct list_head head_of_wild_entry_element_list;  /* struct evfs_entry_head[] */
     int count_of_wild_entries_element;
     int max_pre_userseg;
     lwp_mutex_t mutex;
} __evfs_entries_mgr = {
    .root_of_entries_tree = NULL,
    .head_of_entries_list = { &__evfs_entries_mgr.head_of_entries_list, &__evfs_entries_mgr.head_of_entries_list },
    .count_of_entries = 0,
    .head_of_wild_entry_element_list = { &__evfs_entries_mgr.head_of_wild_entry_element_list, &__evfs_entries_mgr.head_of_wild_entry_element_list },
    .count_of_wild_entries_element = 0,
    .max_pre_userseg = 0,
    .mutex = { PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP },
};

static nsp_status_t __evfs_entries_reference_head(int viewid, struct evfs_entry_head **output, int permission);
static nsp_status_t __evfs_entries_reference_head_by_name(const char *key, struct evfs_entry_head **output, int permission);
static void __evfs_entries_dereference_head(struct evfs_entry_head *entry_head, int permission);
static void __evfs_entries_try_delete_head(struct evfs_entry_head *entry_head, int permission);

static void __evfs_entries_free_head(struct evfs_entry_head *entry_head);
static void __evfs_entries_free_element(struct evfs_entry_element  *entry_element);

static int __evfs_entries_compare_entry_head(const void *left, const void *right)
{
    struct evfs_entry_head *left_entry_head, *right_entry_head;

    left_entry_head = container_of(left, struct evfs_entry_head, leaf_of_entries_tree);
    right_entry_head = container_of(right, struct evfs_entry_head, leaf_of_entries_tree);

    if (left_entry_head->viewid > right_entry_head->viewid) {
        return 1;
    }

    if (left_entry_head->viewid < right_entry_head->viewid) {
        return -1;
    }

    return 0;
}

static int __evfs_entries_compare_named_entry_head(const void *left, const void *right)
{
    struct evfs_entry_head *left_entry_head, *right_entry_head;

    left_entry_head = container_of(left, struct evfs_entry_head, leaf_of_named_entries_tree);
    right_entry_head = container_of(right, struct evfs_entry_head, leaf_of_named_entries_tree);
    
    return strcmp(left_entry_head->key, right_entry_head->key);
}

static struct evfs_entry_head *__evfs_entries_search_head(int viewid)
{
    struct evfs_entry_head find;
    struct avltree_node_t *found;
    struct evfs_entry_head *entry_head;

    find.viewid = viewid;

    lwp_mutex_lock(&__evfs_entries_mgr.mutex);
    found = avlsearch(__evfs_entries_mgr.root_of_entries_tree, &find.leaf_of_entries_tree, &__evfs_entries_compare_entry_head);
    lwp_mutex_unlock(&__evfs_entries_mgr.mutex);

    entry_head = found ? container_of(found, struct evfs_entry_head, leaf_of_entries_tree) : NULL;
    return entry_head;
}

static struct evfs_entry_head *__evfs_entries_search_named_head(const char *key)
{
    struct evfs_entry_head find;
    struct avltree_node_t *found;
    struct evfs_entry_head *entry_head;

    strcpy(find.key, key);

    lwp_mutex_lock(&__evfs_entries_mgr.mutex);
    found = avlsearch(__evfs_entries_mgr.root_of_named_entries_tree, &find.leaf_of_named_entries_tree, &__evfs_entries_compare_named_entry_head);
    lwp_mutex_unlock(&__evfs_entries_mgr.mutex);

    entry_head = found ? container_of(found, struct evfs_entry_head, leaf_of_named_entries_tree) : NULL;
    return entry_head;
}

static nsp_status_t __evfs_entries_check_reference_condition(struct evfs_entry_head *entry_head, int permission)
{
    nsp_status_t status;

    status = NSP_STATUS_SUCCESSFUL;

    lwp_mutex_lock(&entry_head->mutex);
    if (kEvfsEntryNormal == permission) {
        if (kEvfsEntryNormal != entry_head->state) {
            status = posix__makeerror(EBADF); 
        } else {
            entry_head->refcnt++;
            entry_head->io_refcnt++;
        }
    }

    else if (kEvfsEntryOpen == permission) {
        entry_head->refcnt++;
    }

    else if (kEvfsEntryBusy == permission) {
        if (kEvfsEntryNormal != entry_head->state) {
            status = posix__makeerror(EBADF); 
        } else {
            if (0 != entry_head->io_refcnt) {
                status = posix__makeerror(EBUSY);
            } else {
                entry_head->refcnt++;
                entry_head->state = kEvfsEntryBusy;
            }
        }
    }
    lwp_mutex_unlock(&entry_head->mutex);
    return status;
}

static nsp_status_t __evfs_entries_reference_head(int viewid, struct evfs_entry_head **output, int permission)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    entry_head = __evfs_entries_search_head(viewid);
    if (!entry_head) {
        status = posix__makeerror(ENOENT);
    } else {
        status = __evfs_entries_check_reference_condition(entry_head, permission);
    }

    if (NSP_SUCCESS(status) && output) {
        *output = entry_head;
    }
    return status;
}

static nsp_status_t __evfs_entries_reference_head_by_name(const char *key, struct evfs_entry_head **output, int permission)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    entry_head = __evfs_entries_search_named_head(key);
    if (!entry_head) {
        status = posix__makeerror(ENOENT);
    } else {
        status = __evfs_entries_check_reference_condition(entry_head, permission);
    }
    
    if (NSP_SUCCESS(status) && output) {
        *output = entry_head;
    }

    return status;
}

static void __evfs_entries_dereference_head(struct evfs_entry_head *entry_head, int permission) 
{
    int detach_now;

    detach_now = 0;

    lwp_mutex_lock(&entry_head->mutex);

    assert(entry_head->refcnt > 0);

    entry_head->refcnt--;

    if (kEvfsEntryNormal == permission) {
        assert(entry_head->io_refcnt > 0);
        entry_head->io_refcnt--;
    }

    if (kEvfsEntryBusy == permission && kEvfsEntryBusy == entry_head->state) {
        entry_head->state = kEvfsEntryNormal;
    }

    if (0 == entry_head->refcnt && kEvfsEntryCloseWait == entry_head->state) {
        detach_now = 1;
    }

    lwp_mutex_unlock(&entry_head->mutex);

    if (detach_now) {
        __evfs_entries_free_head(entry_head);
    }
}

static void __evfs_entries_try_delete_head(struct evfs_entry_head *entry_head, int permission)
{
    int detach_now;

    detach_now = 0;

    lwp_mutex_lock(&entry_head->mutex);

    entry_head->state = kEvfsEntryCloseWait;

    if (kEvfsEntryMandatory == permission && 0 == entry_head->io_refcnt) {
        entry_head->refcnt = 0;
    }

    if (0 == entry_head->refcnt) {
        detach_now = 1;
    }

    lwp_mutex_unlock(&entry_head->mutex);
    
    if (detach_now) {
        __evfs_entries_free_head(entry_head);
    }
}

static void __evfs_entries_free_element(struct evfs_entry_element  *entry_element)
{
    struct evfs_entry_head *entry_head;

    entry_head = entry_element->entry_head;
    if (!entry_head) {
        zfree(entry_element);
        return;
    }

    /* detach link */
    lwp_mutex_lock(&entry_head->mutex);
    list_del_init(&entry_element->element_of_head_entry);
    entry_head->count_of_elements--;
    lwp_mutex_unlock(&entry_head->mutex);

    if (entry_head->hard_remove_on_detach) {
        evfs_view_move_to_idle(entry_element->view);
    }
    zfree(entry_element);
}

static void __evfs_entries_free_head(struct evfs_entry_head *entry_head)
{
    struct evfs_entry_element  *entry_element;

    /* detach from manager */
    lwp_mutex_lock(&__evfs_entries_mgr.mutex);
    list_del_init(&entry_head->element_of_entries_list);
    __evfs_entries_mgr.root_of_entries_tree = 
            avlremove(__evfs_entries_mgr.root_of_entries_tree, &entry_head->leaf_of_entries_tree, NULL, __evfs_entries_compare_entry_head);
    __evfs_entries_mgr.count_of_entries--;
    if (0 != entry_head->key[0]) {
        __evfs_entries_mgr.root_of_named_entries_tree = 
            avlremove(__evfs_entries_mgr.root_of_named_entries_tree, &entry_head->leaf_of_named_entries_tree, NULL, &__evfs_entries_compare_named_entry_head);
    }
    lwp_mutex_unlock(&__evfs_entries_mgr.mutex);

    /* remove all element which following this head */
    while (NULL != (entry_element = list_first_entry_or_null(&entry_head->head_of_entry_elements, struct evfs_entry_element, element_of_head_entry))) {
        __evfs_entries_free_element(entry_element);
    }

    /* move view of element to idle(hard remove) */
    if (entry_head->hard_remove_on_detach) {
        evfs_view_move_to_idle(entry_head->view);
    }

    /* release mutex */
    lwp_mutex_uninit(&entry_head->mutex);
    /* release head memory */
    zfree(entry_head);
}

static void __evfs_entries_insert_head(struct evfs_entry_head *entry_head)
{
    lwp_mutex_lock(&__evfs_entries_mgr.mutex);
    list_add_tail(&entry_head->element_of_entries_list, &__evfs_entries_mgr.head_of_entries_list);
    __evfs_entries_mgr.count_of_entries++;
    __evfs_entries_mgr.root_of_entries_tree = 
            avlinsert(__evfs_entries_mgr.root_of_entries_tree, &entry_head->leaf_of_entries_tree, &__evfs_entries_compare_entry_head);
    if (0 != entry_head->key[0]) {
        __evfs_entries_mgr.root_of_named_entries_tree = 
            avlinsert(__evfs_entries_mgr.root_of_named_entries_tree, &entry_head->leaf_of_named_entries_tree, &__evfs_entries_compare_named_entry_head);
    }
    lwp_mutex_unlock(&__evfs_entries_mgr.mutex);
}

static void __evfs_entries_insert_wild(struct evfs_entry_element *entry_element)
{
    list_add_tail(&entry_element->element_of_head_entry, &__evfs_entries_mgr.head_of_wild_entry_element_list);
    __evfs_entries_mgr.count_of_wild_entries_element++;
}

static struct evfs_entry_head *__evfs_entries_alloc_head(struct evfs_view_node *view, const char *key)
{
    struct evfs_entry_head *entry_head;

    entry_head = (struct evfs_entry_head *)ztrycalloc(sizeof(*entry_head));
    if (!entry_head) {
        return NULL;
    }

    /* load the entry name */
    if (view) {
        if (!key) {
            evfs_view_read_userdata(view, entry_head->key, 0, sizeof(entry_head->key));
        }
        entry_head->viewid = evfs_view_get_viewid(view);
        entry_head->view = view;
        evfs_view_set_head(view, NULL);
    }

    if (key) {
        strncpy(entry_head->key, key, sizeof(entry_head->key) - 1);
    }

    entry_head->hard_remove_on_detach = 0;
    entry_head->state = kEvfsEntryNormal;
    entry_head->count_of_elements = 0;
    entry_head->refcnt = 0;
    entry_head->io_refcnt = 0;
    INIT_LIST_HEAD(&entry_head->head_of_entry_elements);
    INIT_LIST_HEAD(&entry_head->element_of_entries_list);
    lwp_mutex_init(&entry_head->mutex, 1);
    return entry_head;
}

static struct evfs_entry_element *__evfs_entries_alloc_element_for_head(struct evfs_view_node *view, struct evfs_entry_head *entry_head)
{
    struct evfs_entry_element *entry_element;

    entry_element = (struct evfs_entry_element *)ztrycalloc(sizeof(*entry_element));
    if (!entry_element) {
        return NULL;
    }

    if (view) {
        if (entry_head) {
            evfs_view_set_head(view, entry_head->view);
        }
        entry_element->view = view;
        entry_element->viewid = evfs_view_get_viewid(view);
    }

    if (entry_head) {
        entry_element->entry_head = entry_head;

        lwp_mutex_lock(&entry_head->mutex);
        list_add_tail(&entry_element->element_of_head_entry, &entry_head->head_of_entry_elements);
        entry_head->count_of_elements++;
        lwp_mutex_unlock(&entry_head->mutex);
    }

    return entry_element;
}

static void __evfs_entries_raw_recognize(struct evfs_view_node *view)
{
    struct evfs_entry_head *entry_head;
    struct evfs_entry_element  *entry_element;

    if (evfs_view_looks_like_head(view)) {
        entry_head = __evfs_entries_alloc_head(view, NULL);
        if (!entry_head) {
            return;
        }
        __evfs_entries_insert_head(entry_head);
    } else {
        /* push non-head entry to wild list at first */
        entry_element = (struct evfs_entry_element *)ztrymalloc(sizeof(*entry_element));
        if (!entry_element) {
            return;
        }
        entry_element->view = view;
        entry_element->viewid = evfs_view_get_viewid(view);
        __evfs_entries_insert_wild(entry_element);
    }
}

static void __evfs_entries_init()
{
    struct evfs_entry_head *entry_head;
    struct list_head *pos, *n;
    struct list_head *pos_wild, *n_wild;
    struct evfs_entry_element  *entry_element;
    int next_view_id;

    list_for_each_safe(pos, n, &__evfs_entries_mgr.head_of_entries_list) {
        entry_head = container_of(pos, struct evfs_entry_head, element_of_entries_list);
        next_view_id = evfs_view_get_next_cluster_id(entry_head->view);
        while (next_view_id > 0) {
            entry_element = NULL;
            /* travese wild elements */
            list_for_each_safe(pos_wild, n_wild, &__evfs_entries_mgr.head_of_wild_entry_element_list) {
                entry_element = container_of(pos_wild, struct evfs_entry_element, element_of_head_entry);
                if (entry_element->viewid == next_view_id) {
                    break;
                }
                entry_element = NULL;
            }
            next_view_id = 0;

            /* found element of this head entry */
            if (entry_element) {
                entry_element->entry_head = entry_head;
                list_del_init(&entry_element->element_of_head_entry);
                __evfs_entries_mgr.count_of_wild_entries_element--;
                list_add_tail(&entry_element->element_of_head_entry, &entry_head->head_of_entry_elements);
                entry_head->count_of_elements++;
                next_view_id = evfs_view_get_next_cluster_id(entry_element->view);
            } else {
                /* sub element no found, entire head entry are no longer effective */
                entry_head->hard_remove_on_detach = 1;
                __evfs_entries_try_delete_head(entry_head, kEvfsEntryMandatory);
            }
        }
    }

    /* these wild element can not found their head entry, remove them */
    list_for_each_safe(pos_wild, n_wild, &__evfs_entries_mgr.head_of_wild_entry_element_list) {
        entry_element = container_of(pos_wild, struct evfs_entry_element, element_of_head_entry);
        __evfs_entries_free_element(entry_element);
    }

    __evfs_entries_mgr.max_pre_userseg = evfs_view_get_max_pre_userseg();
}

nsp_status_t evfs_entries_create()
{
    nsp_status_t status;

    status = evfs_view_create();
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    __evfs_entries_init();
    return status;
}

nsp_status_t evfs_entries_load()
{
    nsp_status_t status;

    status = evfs_view_load(__evfs_entries_raw_recognize);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    __evfs_entries_init();
    return status;
}

extern void evfs_entries_uninit()
{
    struct evfs_entry_head *entry_head;

    lwp_mutex_lock(&__evfs_entries_mgr.mutex);

    while (NULL != (entry_head = list_first_entry_or_null(&__evfs_entries_mgr.head_of_entries_list, struct evfs_entry_head, element_of_entries_list))) {
        __evfs_entries_try_delete_head(entry_head, kEvfsEntryMandatory);
    }

    lwp_mutex_unlock(&__evfs_entries_mgr.mutex);

    evfs_view_uninit();
}

nsp_status_t evfs_entries_create_one(const char *key, int *entry_id)
{
    struct evfs_view_node *view;
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    if (!entry_id) {
        return posix__makeerror(EINVAL);
    }

    if (key) {
        entry_head = __evfs_entries_search_named_head(key);
        if (entry_head) {
            return posix__makeerror(EEXIST);
        }
    }

    view = NULL;
    entry_head = NULL;
    status = NSP_STATUS_SUCCESSFUL;
    do {
        view = evfs_view_acquire_idle();
        if (!view) {
            status = posix__makeerror(ENOSPC);
            break;
        }

        entry_head = __evfs_entries_alloc_head(view, key);
        if (!entry_head) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        /* initial states always have 32 bytes occupy by entry name */
        status = evfs_view_set_head_data_seg_size(entry_head->view, sizeof(entry_head->key));
        if (!NSP_SUCCESS(status)) {
            break;
        }

        status = evfs_view_write_userdata(entry_head->view, entry_head->key, 0, sizeof(entry_head->key));
        if (!NSP_SUCCESS(status)) {
            break;
        }

        __evfs_entries_insert_head(entry_head);
        *entry_id = evfs_view_get_viewid(view);
    } while (0);

    if (!NSP_SUCCESS(status)) {
        if (entry_head) {
            zfree(entry_head);
        }
        
        if (view) {
            evfs_view_move_to_idle(view);
        }
    } else {
        status = __evfs_entries_reference_head(entry_head->viewid, NULL, kEvfsEntryOpen);
    }

    return status;
}

nsp_status_t evfs_entries_open_one(int entry_id)
{
    return __evfs_entries_reference_head(entry_id, NULL, kEvfsEntryOpen);
}

nsp_status_t evfs_entries_open_one_by_name(const char *key, int *entry_id)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    if (!key) {
        return posix__makeerror(EINVAL);
    }

    status = __evfs_entries_reference_head_by_name(key, &entry_head, kEvfsEntryOpen);
    if (!NSP_SUCCESS(status)) {
        return posix__makeerror(ENOENT); 
    }

    if (entry_id) {
        *entry_id = entry_head->viewid;
    }
    return NSP_STATUS_SUCCESSFUL;
}

void evfs_entries_close_one(int entry_id)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryOpen);
    if (NSP_SUCCESS(status)) {
        /* double dereference to decrease the count which incerased by open function */
        __evfs_entries_dereference_head(entry_head, kEvfsEntryOpen);
        __evfs_entries_dereference_head(entry_head, kEvfsEntryOpen);
    }
}

static nsp_status_t __evfs_entries_alloc_idle_element_for_head(struct evfs_entry_head *entry_head, int quota_of_clusters_require)
{
    struct evfs_view_node **views;
    nsp_status_t status;
    struct evfs_entry_element *entry_element, *entry_last_element;
    struct list_head *pos, *n;
    int i;
    int acquire_cover_head_entry;

    if (!entry_head) {
        return posix__makeerror(EINVAL);
    }

    if (0 == quota_of_clusters_require) {
        return NSP_STATUS_SUCCESSFUL;
    }

    views = (struct evfs_view_node **)ztrycalloc(quota_of_clusters_require * sizeof(void *));
    if (!views) {
        return posix__makeerror(ENOMEM);
    }

    status = evfs_view_acquire_idle_more(quota_of_clusters_require, views);
    if (!NSP_SUCCESS(status)) {
        zfree(views);
        return status;
    }

    /* check the element list head, if the list is empty, then link the head entry and the 1st element */
    entry_last_element = NULL;
    acquire_cover_head_entry = 1;
    if (!list_empty(&entry_head->head_of_entry_elements)) {
        entry_last_element = list_last_entry(&entry_head->head_of_entry_elements, struct evfs_entry_element, element_of_head_entry);
        acquire_cover_head_entry = 0;
    }

    for (i = 0; i < quota_of_clusters_require; i++) {
        entry_element = __evfs_entries_alloc_element_for_head(views[i], entry_head);
        if (!entry_element) {
            break;
        }

        evfs_view_set_next(entry_last_element ? entry_last_element->view : entry_head->view, views[i]);
        entry_last_element = entry_element;
    }

    zfree(views);

    /* failed on allocate memory for entry head */
    if (!entry_element) {
        while (NULL != (entry_element = list_first_entry_or_null(&entry_head->head_of_entry_elements, struct evfs_entry_element, element_of_head_entry ))) {
            __evfs_entries_free_element(entry_element);
        }
        status = posix__makeerror(ENOMEM);
    } else {
        list_for_each_safe(pos, n, &entry_head->head_of_entry_elements) {
            entry_element = container_of(pos, struct evfs_entry_element, element_of_head_entry);
            status = evfs_view_write_head(entry_element->view);
            if (!NSP_SUCCESS(status)) {
                break;
            }
        }

        if (acquire_cover_head_entry && NSP_SUCCESS(status)) {
            status = evfs_view_write_head(entry_head->view);
        }
    }

    return status;
}

nsp_status_t evfs_entries_truncate(int entry_id, int size)
{
    int quota_of_clusters_require, quota_of_elements_require;
    struct evfs_entry_head *entry_head;
    int i;
    struct evfs_entry_element *entry_element, *prev_entry_element;
    int quota_of_elements_diff;
    nsp_status_t status;
    int current_data_seg_size;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryBusy);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    current_data_seg_size = evfs_view_get_data_seg_size(entry_head->view);

    do {
        /* size not change */
        if (size == current_data_seg_size) {
            break;
        }

        quota_of_clusters_require = evfs_view_transfer_size_to_cluster_count(size);
        if (quota_of_clusters_require <= 0) {
            status = posix__makeerror(EINVAL);
            break;
        }
        quota_of_elements_require = quota_of_clusters_require - 1; /* eliminate head */

        /* elements didn't need any modify */
        if (quota_of_elements_require == entry_head->count_of_elements) {
            /* adjust user data seg length in head entry */
            status = evfs_view_set_head_data_seg_size(entry_head->view, size);
            break;
        }

        /* reduce entry size, release views from entry tail */
        if (quota_of_elements_require < entry_head->count_of_elements) {
            quota_of_elements_diff = entry_head->count_of_elements - quota_of_elements_require;
            entry_element = container_of(entry_head->head_of_entry_elements.prev, struct evfs_entry_element, element_of_head_entry);
            for (i = 0; i < quota_of_elements_diff; i++) {
                prev_entry_element = container_of(entry_element->element_of_head_entry.prev, struct evfs_entry_element, element_of_head_entry);
                __evfs_entries_free_element(entry_element);
                entry_element = prev_entry_element;
            }

            /* mark the next view of head or element */
            if ( entry_head->count_of_elements > 0 ) {
                entry_element = container_of(entry_head->head_of_entry_elements.prev, struct evfs_entry_element, element_of_head_entry);
                evfs_view_set_next(entry_element->view, NULL);
            } else {
                /* all elements have been removed, change the link region of head entry */
                evfs_view_set_next(entry_head->view, NULL);
            }

            /* adjust user data seg length in head entry */
            status = evfs_view_set_head_data_seg_size(entry_head->view, size);
            break;
        }

        if (quota_of_elements_require > entry_head->count_of_elements) {
            /* adjust user data seg length in head entry */
            status = evfs_view_set_head_data_seg_size(entry_head->view, size);
            if (!NSP_SUCCESS(status)) {
                break;
            }
            status = __evfs_entries_alloc_idle_element_for_head(entry_head, quota_of_elements_require);
            break;
        }
    } while(0);

    __evfs_entries_dereference_head(entry_head, kEvfsEntryBusy);
    return status;
}

void evfs_entries_flush(int entry_id)
{
    struct evfs_entry_head *entry_head;
    struct evfs_entry_element *entry_element;
    struct list_head *pos, *n;
    nsp_status_t status;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!NSP_SUCCESS(status)) {
        return;
    }

    list_for_each_safe(pos, n, &entry_head->head_of_entry_elements) {
        entry_element = container_of(pos, struct evfs_entry_element, element_of_head_entry);
        evfs_view_flush(entry_element->view);
    }
    evfs_view_flush(entry_head->view);

    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
}

nsp_status_t evfs_entries_lock_elements(int entry_id, int offset, int size)
{
    //int quota_of_clusters_require;
    struct evfs_entry_head *entry_head;
    int data_seg_size;
    int cpoff;
    nsp_status_t status;

    if (size <= 0) {
        return posix__makeerror(EINVAL);
    }

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    cpoff = sizeof(entry_head->key) + offset;
    data_seg_size = evfs_view_get_data_seg_size(entry_head->view);
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);

    if (cpoff + size > data_seg_size) {
        status = evfs_entries_truncate(entry_id, cpoff + size);
    }
    return status;
}

static int __evfs_entries_write_element(struct evfs_entry_element *entry_element, const char *data, int element_inner_offset,int size)
{
    int cpremain;
    int cpoff;
    int cpsize;
    struct evfs_entry_element *entry_next_element;
    nsp_status_t status;

    if (!entry_element || !data || size <= 0) {
        return 0;
    }

    cpoff = 0;
    cpremain = size;
    entry_next_element = entry_element;
    status = NSP_STATUS_SUCCESSFUL;
    cpsize = min(cpremain, __evfs_entries_mgr.max_pre_userseg);

    evfs_view_write_userdata(entry_next_element->view, data + cpoff, element_inner_offset, cpsize);
    cpoff += cpsize;
    cpremain -= cpsize;
    if (entry_next_element->element_of_head_entry.next == &entry_next_element->entry_head->head_of_entry_elements) {
        entry_next_element = NULL;
    } else {
        entry_next_element = container_of(entry_next_element->element_of_head_entry.next, struct evfs_entry_element, element_of_head_entry);
    }

    while (entry_next_element && cpremain > 0) {
        cpsize = min(cpremain, __evfs_entries_mgr.max_pre_userseg);
        evfs_view_write_userdata(entry_next_element->view, data + cpoff, 0, cpsize);
        cpoff += cpsize;
        cpremain -= cpsize;
        if (entry_next_element->element_of_head_entry.next == &entry_next_element->entry_head->head_of_entry_elements) {
            entry_next_element = NULL;
        } else {
            entry_next_element = container_of(entry_next_element->element_of_head_entry.next, struct evfs_entry_element, element_of_head_entry);
        }
    }

    if (cpremain > 0) {
        status = posix__makeerror(ENOSPC);
    }
    
    return NSP_SUCCESS(status) ? cpoff : status;
}

int evfs_entries_write_data(int entry_id, const char *data, int offset, int size)
{
    struct evfs_entry_head *entry_head;
    struct evfs_entry_element *entry_next_element;
    int cpsize;
    int cpremain;
    int cpoff;
    int element_inner_offset;
    struct list_head *pos, *n;
    int cp_ele_size;
    nsp_status_t status;
    int wrcb;

    if (entry_id <= 0 || !data || size <= 0) {
        return posix__makeerror(EINVAL);
    }

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    entry_next_element = NULL;
    cpsize = 0;
    cpremain = size;
    cpoff = offset + sizeof(entry_head->key); /* real copy offset contain the offset of key length in head */
    element_inner_offset = 0;

    /* write data from head view */
    if (cpoff < __evfs_entries_mgr.max_pre_userseg ) {
        cpsize = min( __evfs_entries_mgr.max_pre_userseg - cpoff, size);
        evfs_view_write_userdata(entry_head->view, data, cpoff, cpsize);
        cpremain -= cpsize;
        /* remain data copy from the head of next piece */
        element_inner_offset = 0;
        if (cpremain > 0) {
            entry_next_element = list_empty(&entry_head->head_of_entry_elements) ? 
                    NULL : container_of(entry_head->head_of_entry_elements.next, struct evfs_entry_element, element_of_head_entry);
        }
    } else {
        element_inner_offset = cpoff - __evfs_entries_mgr.max_pre_userseg;
        list_for_each_safe(pos, n, &entry_head->head_of_entry_elements) {
            if (element_inner_offset < __evfs_entries_mgr.max_pre_userseg) {
                entry_next_element = container_of(pos, struct evfs_entry_element, element_of_head_entry);
                break;
            }
            element_inner_offset -= __evfs_entries_mgr.max_pre_userseg;
        }
    }

    wrcb = 0;
    do {
        if (!entry_next_element) {
            if (size - cpsize > 0) {
                wrcb = posix__makeerror(ENOSPC);
                break;
            }
            wrcb = cpsize;
            break;
        }

        if (0 == cpremain) {
            wrcb = cpsize;
            break;
        }

        cp_ele_size = __evfs_entries_write_element(entry_next_element, data + cpsize, element_inner_offset, cpremain);
        if (cp_ele_size <= 0) {
            wrcb = cp_ele_size;
            break;
        }

        wrcb = cpsize + cp_ele_size;
    } while (0);

    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return wrcb;
}

static int __evfs_entries_read_element(struct evfs_entry_element *entry_element, char *data, int element_inner_offset,int size)
{
    int cpremain;
    int cpoff;
    int cpsize;
    struct evfs_entry_element *entry_next_element;
    nsp_status_t status;

    if (!entry_element || !data || size <= 0) {
        return posix__makeerror(EINVAL);
    }

    cpoff = 0;
    cpremain = size;
    entry_next_element = entry_element;
    status = NSP_STATUS_SUCCESSFUL;
    cpsize = min(cpremain, __evfs_entries_mgr.max_pre_userseg);

    evfs_view_read_userdata(entry_next_element->view, data + cpoff, element_inner_offset, cpsize);
    cpoff += cpsize;
    cpremain -= cpsize;
    if (entry_next_element->element_of_head_entry.next == &entry_next_element->entry_head->head_of_entry_elements) {
        entry_next_element = NULL;
    } else {
        entry_next_element = container_of(entry_next_element->element_of_head_entry.next, struct evfs_entry_element, element_of_head_entry);
    }

    while (entry_next_element && cpremain > 0) {
        cpsize = min(cpremain, __evfs_entries_mgr.max_pre_userseg);
        evfs_view_read_userdata(entry_next_element->view, data + cpoff, 0, cpsize);
        cpoff += cpsize;
        cpremain -= cpsize;
        if (entry_next_element->element_of_head_entry.next == &entry_next_element->entry_head->head_of_entry_elements) {
            entry_next_element = NULL;
        } else {
            entry_next_element = container_of(entry_next_element->element_of_head_entry.next, struct evfs_entry_element, element_of_head_entry);
        }
    }

    if (cpremain > 0) {
        status = posix__makeerror(ENOSPC);
    }
    
    return NSP_SUCCESS(status) ? cpoff : status;
}

int evfs_entries_read_data(int entry_id, char *data, int offset, int size)
{
    struct evfs_entry_head *entry_head;
    struct evfs_entry_element *entry_next_element;
    int cpoff;
    int cpsize;
    int cpremain;
    int element_inner_offset;
    struct list_head *pos, *n;
    nsp_status_t status;
    int data_seg_size;
    int rdcb;
    int cp_ele_size;

    if (entry_id < 0 || !data || offset < 0 || size <= 0 ) {
        return posix__makeerror(EINVAL);
    }

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    data_seg_size = evfs_view_get_data_seg_size(entry_head->view);
    if (data_seg_size <= 0) {
        __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
        return 0;
    }

    cpsize = 0;
    status = NSP_STATUS_SUCCESSFUL;
    element_inner_offset = 0;
    entry_next_element = NULL;
    cpoff = sizeof(entry_head->key) + offset;
    cpremain = min(data_seg_size - cpoff, size);
    if (cpremain <= 0) {
        __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
        return posix__makeerror(EINVAL);
    }

    /* write data from head view */
    if (cpoff < __evfs_entries_mgr.max_pre_userseg) {
        cpsize = min(__evfs_entries_mgr.max_pre_userseg - cpoff, cpremain);
        status = evfs_view_read_userdata(entry_head->view, data, cpoff, cpsize);
        if (!NSP_SUCCESS(status)) {
            entry_next_element = NULL;
        } else {
            entry_next_element = list_empty(&entry_head->head_of_entry_elements) ? 
                        NULL : container_of(entry_head->head_of_entry_elements.next, struct evfs_entry_element, element_of_head_entry);
        }
        cpremain -= cpsize;
    } else {
        element_inner_offset = cpoff - __evfs_entries_mgr.max_pre_userseg;
        list_for_each_safe(pos, n, &entry_head->head_of_entry_elements) {
            if (element_inner_offset < __evfs_entries_mgr.max_pre_userseg) {
                entry_next_element = container_of(pos, struct evfs_entry_element, element_of_head_entry);
                break;
            }
            element_inner_offset -= __evfs_entries_mgr.max_pre_userseg;
        }
    }

    do {
        if (!entry_next_element) {
            /* input buffer have enough space to save data but element of entries are insufficient */
            if (cpremain > 0 && cpsize < (data_seg_size - sizeof(entry_head->key))) {
                rdcb = posix__makeerror(ENOSPC);
                break;
            }
            rdcb = cpsize;
            break;
        }

        if (0 == cpremain) {
            rdcb = cpsize;
            break;
        }

        cp_ele_size = __evfs_entries_read_element(entry_next_element, data + cpsize, element_inner_offset, cpremain);
        if (!NSP_SUCCESS(cp_ele_size)) {
            rdcb = cp_ele_size;
            break;
        }

        rdcb = cpsize + cp_ele_size;
    } while(0);
    
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return rdcb;
}

int evfs_entries_query_key(int entry_id, char *key)
{
    struct evfs_entry_head *entry_head;
    size_t n;
    nsp_status_t status;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    n = strlen(entry_head->key);
    if (key && n > 0 ) {
        strcpy(key, entry_head->key);
    }

    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return n;
}

int evfs_entries_query_user_seg_size(int entry_id)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;
    int data_seg_size;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    data_seg_size = evfs_view_get_data_seg_size(entry_head->view);
    if (data_seg_size > 0) {
        data_seg_size -= sizeof(entry_head->key);
    }
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return data_seg_size;
}

int evfs_entries_iterate(struct list_head **pos)
{
    int viewid;
     struct evfs_entry_head *entry_head;

    if (!pos) {
        return -1;
    }

    if (!*pos) {
        *pos = __evfs_entries_mgr.head_of_entries_list.next;
    }

    while (*pos != &__evfs_entries_mgr.head_of_entries_list) {
        entry_head = container_of(*pos, struct evfs_entry_head, element_of_entries_list);
        *pos = (*pos)->next;
        if (entry_head->state == kEvfsEntryNormal) { // iterate active entry
            viewid = entry_head->viewid;
            return viewid;
        }
    }

    return -1;
}

nsp_status_t evfs_entries_soft_delete(int entry_id)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    __evfs_entries_try_delete_head(entry_head, kEvfsEntryNormal);
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return status;
}

nsp_status_t evfs_entries_hard_delete(int entry_id)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    status = __evfs_entries_reference_head(entry_id, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    entry_head->hard_remove_on_detach = 1;
    __evfs_entries_try_delete_head(entry_head, kEvfsEntryNormal);
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return status;
}

nsp_status_t evfs_entries_hard_delete_by_name(const char *key)
{
    struct evfs_entry_head *entry_head;
    nsp_status_t status;

    status = __evfs_entries_reference_head_by_name(key, &entry_head, kEvfsEntryNormal);
    if (!entry_head || !NSP_SUCCESS(status)) {
        return status;
    }

    entry_head->hard_remove_on_detach = 1;
    __evfs_entries_try_delete_head(entry_head, kEvfsEntryNormal);
    __evfs_entries_dereference_head(entry_head, kEvfsEntryNormal);
    return status;
}

nsp_status_t evfs_entries_soft_stat(struct evfs_entries_stat *stat)
{
    if (!stat) {
        return posix__makeerror(EINVAL);
    }

    evfs_view_get_count(&stat->idle_view_count, &stat->busy_view_count);
    stat->total_entry_count = __evfs_entries_mgr.count_of_entries;
    return NSP_STATUS_SUCCESSFUL;
}
