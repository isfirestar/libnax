#include "view.h"

#include "cache.h"
#include "cluster.h"

#include "zmalloc.h"
#include "threading.h"
#include "atom.h"
#include "clist.h"
#include "avltree.h"

#include <stdlib.h>
#include <errno.h>

struct evfs_view_node
{
    int viewid;
    struct list_head element_of_view_list;
    struct avltree_node_t leaf_of_view_tree;
    struct evfs_cluster cluster;
};

static struct {
    struct avltree_node_t *root_of_view_tree;
    struct list_head head_of_view_busylist;
    struct list_head head_of_view_idlelist;
    int count_of_busy_views;
    int count_of_idle_views;
    lwp_mutex_t mutex;
    int total_cluster_count;
    int cluster_size;
    int max_pre_userseg;
} __evfs_view_mgr = {
    .root_of_view_tree = NULL,
    .head_of_view_busylist = { &__evfs_view_mgr.head_of_view_busylist,  &__evfs_view_mgr.head_of_view_busylist },
    .head_of_view_idlelist = { &__evfs_view_mgr.head_of_view_idlelist,  &__evfs_view_mgr.head_of_view_idlelist },
    .count_of_busy_views = 0,
    .count_of_idle_views = 0,
    .total_cluster_count = 0,
    .cluster_size = 0,
    .max_pre_userseg = 0,
};

static int __evfs_view_compare(const void *left, const void *right)
{
    evfs_view_pt left_view, right_view;

    left_view = container_of(left, struct evfs_view_node, leaf_of_view_tree);
    right_view = container_of(right, struct evfs_view_node, leaf_of_view_tree);

    if (left_view->viewid > right_view->viewid) {
        return 1;
    }

    if (left_view->viewid < right_view->viewid) {
        return -1;
    }

    return 0;
}

static void __evfs_view_insert_new_idle(evfs_view_pt view)
{
    lwp_mutex_lock(&__evfs_view_mgr.mutex);
    list_add_tail(&view->element_of_view_list, &__evfs_view_mgr.head_of_view_idlelist);
    __evfs_view_mgr.root_of_view_tree = avlinsert(__evfs_view_mgr.root_of_view_tree, &view->leaf_of_view_tree, &__evfs_view_compare);
    __evfs_view_mgr.count_of_idle_views++;
    lwp_mutex_unlock(&__evfs_view_mgr.mutex);
}

nsp_status_t evfs_view_create()
{
    int cluster_count;
    int i;
    nsp_status_t status;
    evfs_view_pt view;
    int expect;
    struct evfs_cache_stat stat;

    if (atom_get(&__evfs_view_mgr.total_cluster_count) > 0 ) {
        return EEXIST;
    }

    status = evfs_cache_hard_state(&stat);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    cluster_count = stat.hard_cluster_count;
    expect = 0;
    if (!atom_compare_exchange_strong(&__evfs_view_mgr.total_cluster_count, &expect, cluster_count)) {
        return EEXIST;
    }
    lwp_mutex_init(&__evfs_view_mgr.mutex, 1);

    status = NSP_STATUS_SUCCESSFUL;
    for (i = 1; i <= cluster_count; i++) {
        view = (evfs_view_pt )ztrymalloc(sizeof(*view));
        if (!view) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        view->viewid = i;
        __evfs_view_insert_new_idle(view);
    }

    /* on failed, delete all view items */
    if (!NSP_SUCCESS(status)) {
        while (NULL != (view = list_first_entry_or_null(&__evfs_view_mgr.head_of_view_idlelist, struct evfs_view_node, element_of_view_list))) {
            list_del_init(&view->element_of_view_list);
            __evfs_view_mgr.root_of_view_tree = avlremove(__evfs_view_mgr.root_of_view_tree, &view->leaf_of_view_tree, NULL, &__evfs_view_compare);
            __evfs_view_mgr.count_of_idle_views--;
            zfree(view);
        }
    } else {
        __evfs_view_mgr.cluster_size = stat.hard_cluster_size;
        __evfs_view_mgr.max_pre_userseg = stat.hard_max_pre_userseg;
    }
    return status;
}

static void __evfs_view_recognize_and_move_to_busy(on_view_loaded_t on_loaded)
{
    struct list_head *pos, *n;
    evfs_view_pt view;

    list_for_each_safe(pos, n, &__evfs_view_mgr.head_of_view_idlelist) {
        view = container_of(pos, struct evfs_view_node, element_of_view_list);
        if (evfs_cluster_looks_like_busy(&view->cluster)) {
            list_del_init(&view->element_of_view_list);
            __evfs_view_mgr.count_of_idle_views--;
            list_add_tail(&view->element_of_view_list, &__evfs_view_mgr.head_of_view_busylist);
            __evfs_view_mgr.count_of_busy_views++;
            if (on_loaded) {
                on_loaded(view);
            }
        }
    }
}

nsp_status_t evfs_view_load(on_view_loaded_t on_loaded)
{
    int cluster_count;
    int i;
    struct evfs_cluster cluster;
    nsp_status_t status;
    evfs_view_pt view;
    int expect;
    struct evfs_cache_stat stat;

    status = evfs_cache_hard_state(&stat);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    expect = 0;
    cluster_count = stat.hard_cluster_count;
    if (!atom_compare_exchange_strong(&__evfs_view_mgr.total_cluster_count, &expect, cluster_count)) {
        return EEXIST;
    }
    lwp_mutex_init(&__evfs_view_mgr.mutex, 1);

    status = NSP_STATUS_SUCCESSFUL;

    /* load and push all views to idel list */
    for (i = 1; i <= cluster_count; i++) {
        status = evfs_cache_read_head_directly(i, &cluster);
        if (!NSP_SUCCESS(status)) {
            break;
        }
        view = (evfs_view_pt )ztrymalloc(sizeof(*view));
        if (!view) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        view->viewid = i;
        memcpy(&view->cluster, &cluster, sizeof(cluster));
        __evfs_view_insert_new_idle(view);
    }

    /* on failed delete all view items */
    if (!NSP_SUCCESS(status)) {
        while (NULL != (view = list_first_entry_or_null(&__evfs_view_mgr.head_of_view_idlelist, struct evfs_view_node, element_of_view_list))) {
            list_del_init(&view->element_of_view_list);
            __evfs_view_mgr.root_of_view_tree = avlremove(__evfs_view_mgr.root_of_view_tree, &view->leaf_of_view_tree, NULL, &__evfs_view_compare);
            zfree(view);
        }
    } else {
        __evfs_view_recognize_and_move_to_busy(on_loaded);
        __evfs_view_mgr.cluster_size = stat.hard_cluster_size;
        __evfs_view_mgr.max_pre_userseg = stat.hard_max_pre_userseg;
    }
    return status;
}

nsp_status_t evfs_view_expand()
{
    int next_cluster_id;
    int number_of_clusters_expanded;
    int i;
    evfs_view_pt view;

    number_of_clusters_expanded = evfs_cache_expand(&next_cluster_id);
    if (number_of_clusters_expanded <= 0) {
        return NSP_STATUS_FATAL;
    }

    for (i = 0; i < number_of_clusters_expanded; i++) {
        /* NOT report a error even when memory allocation failed, just ignore one view */
        view = (evfs_view_pt )ztrymalloc(sizeof(*view));
        if (view) {
            view->viewid = next_cluster_id;
            view->cluster.data_seg_size = 0;
            view->cluster.head_cluster_id = 0;
            view->cluster.next_cluster_id = 0;
            __evfs_view_insert_new_idle(view);
        }
        next_cluster_id++;
    }

    return NSP_STATUS_SUCCESSFUL;
}

void evfs_view_uninit()
{
    evfs_view_pt view;

    if (atom_exchange(&__evfs_view_mgr.total_cluster_count, 0) <= 0) {
        return;
    }

    lwp_mutex_lock(&__evfs_view_mgr.mutex);

    while (NULL != (view = list_first_entry_or_null(&__evfs_view_mgr.head_of_view_busylist, struct evfs_view_node, element_of_view_list))) {
        list_del_init(&view->element_of_view_list);
        __evfs_view_mgr.count_of_busy_views--;
        __evfs_view_mgr.root_of_view_tree = avlremove(__evfs_view_mgr.root_of_view_tree, &view->leaf_of_view_tree, NULL, &__evfs_view_compare);
        zfree(view);
    }

    while (NULL != (view = list_first_entry_or_null(&__evfs_view_mgr.head_of_view_idlelist, struct evfs_view_node, element_of_view_list))) {
        list_del_init(&view->element_of_view_list);
        __evfs_view_mgr.count_of_idle_views--;
        __evfs_view_mgr.root_of_view_tree = avlremove(__evfs_view_mgr.root_of_view_tree, &view->leaf_of_view_tree, NULL, &__evfs_view_compare);
        zfree(view);
    }

    lwp_mutex_unlock(&__evfs_view_mgr.mutex);
}

 void evfs_view_get_count(int *freeview, int *busyview)
 {
    if (atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return;
    }

    lwp_mutex_lock(&__evfs_view_mgr.mutex);

    if (freeview) {
        *freeview = __evfs_view_mgr.count_of_idle_views;
    }

    if (busyview) {
        *busyview = __evfs_view_mgr.count_of_busy_views;
    }

    lwp_mutex_unlock(&__evfs_view_mgr.mutex);
 }

nsp_status_t evfs_view_move_to_idle(evfs_view_pt view)
{
    if (!view || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return posix__makeerror(EINVAL);
    }

    view->cluster.data_seg_size = 0;
    view->cluster.head_cluster_id = 0;
    view->cluster.next_cluster_id = 0;

    lwp_mutex_lock(&__evfs_view_mgr.mutex);
    list_del_init(&view->element_of_view_list);
    __evfs_view_mgr.count_of_busy_views--;
    list_add_tail(&view->element_of_view_list, &__evfs_view_mgr.head_of_view_idlelist);
    __evfs_view_mgr.count_of_idle_views++;
    lwp_mutex_unlock(&__evfs_view_mgr.mutex);

    return evfs_cache_write_head(view->viewid, &view->cluster);
}

evfs_view_pt evfs_view_acquire_idle()
{
    evfs_view_pt view;
    struct list_head *node;
    nsp_status_t status;

    if (atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return NULL;
    }

    lwp_mutex_lock(&__evfs_view_mgr.mutex);

    while (__evfs_view_mgr.count_of_idle_views <= 0) {
        lwp_mutex_unlock(&__evfs_view_mgr.mutex);
        status = evfs_view_expand();
        if (!NSP_SUCCESS(status)) {
            return NULL;
        }
        lwp_mutex_lock(&__evfs_view_mgr.mutex);
    }

    node = __evfs_view_mgr.head_of_view_idlelist.next;
    view = container_of(node, struct evfs_view_node, element_of_view_list);
    /* erase from idle */
    list_del_init(&view->element_of_view_list);
    __evfs_view_mgr.count_of_idle_views--;
    /* add to busy */
    list_add_tail(&view->element_of_view_list, &__evfs_view_mgr.head_of_view_busylist);
    __evfs_view_mgr.count_of_busy_views++;

    lwp_mutex_unlock(&__evfs_view_mgr.mutex);

    return view;
}

nsp_status_t evfs_view_acquire_idle_more(int amount, evfs_view_pt *views)
{
    int i;
    int j;

    if (!views || amount <= 0 || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0 ) {
        return posix__makeerror(EINVAL);
    }

    for (i = 0; i < amount; i++) {
        /* push to output */
        views[i] = evfs_view_acquire_idle();
        if (!views[i]) {
            break;
        }
    }

    /* at least one view allocated failed */
    if (i < amount) {
        for (j = 0; j < i; j++) {
            evfs_view_move_to_idle(views[j]);
        }
        return posix__makeerror(ENOMEM);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t evfs_view_write_head(evfs_view_pt view)
{
    if (!view || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_write_head(view->viewid, &view->cluster);
}

nsp_status_t evfs_view_write_userdata(const evfs_view_pt view, const void *buffer, int offset, int length)
{
    if (!view || !buffer || offset < 0 || length <= 0 || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return posix__makeerror(EINVAL);
    }

    /* neither a head view nor a element view */
    if (0 == view->cluster.data_seg_size && 0 == view->cluster.head_cluster_id) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_write_userdata(view->viewid, buffer, offset, length);
}

nsp_status_t evfs_view_read_userdata(const evfs_view_pt view, void *buffer, int offset, int length)
{
    if (!view || !buffer || offset < 0 || length <= 0 || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return posix__makeerror(EINVAL);
    }

    if (0 == view->cluster.data_seg_size && 0 == view->cluster.head_cluster_id) {
        return posix__makeerror(EINVAL);
    }

    return evfs_cache_read_userdata(view->viewid, buffer, offset, length);
}

void evfs_view_flush(evfs_view_pt view)
{
    if (!view || atom_get(&__evfs_view_mgr.total_cluster_count) <= 0) {
        return;
    }

    evfs_cache_flush_block(view->viewid, 1);
}

nsp_status_t evfs_view_set_next(evfs_view_pt view, const evfs_view_pt next_view)
{
    if (!view) {
        return posix__makeerror(EINVAL);
    }

    view->cluster.next_cluster_id = (NULL != next_view) ? next_view->viewid : 0;
    return evfs_cache_write_head(view->viewid, &view->cluster);
}

nsp_status_t evfs_view_set_head(evfs_view_pt view, const evfs_view_pt head_view)
{
    if (!view) {
        return posix__makeerror(EINVAL);
    }

    view->cluster.head_cluster_id = (NULL != head_view) ? head_view->viewid : view->viewid;
    return evfs_cache_write_head(view->viewid, &view->cluster);
}

int evfs_view_get_next_cluster_id(const evfs_view_pt view)
{
    return (NULL != view) ? view->cluster.next_cluster_id : 0;
}

nsp_status_t evfs_view_set_head_data_seg_size(evfs_view_pt view, int size)
{
    int head_data_seg_size;

    if (!view) {
        return posix__makeerror(EINVAL);
    }

    head_data_seg_size = (size <= 0 ? size : (size | 0x80000000));

    if (view->cluster.data_seg_size != head_data_seg_size) {
        view->cluster.data_seg_size = head_data_seg_size;
        return evfs_cache_write_head(view->viewid, &view->cluster);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t evfs_view_set_element_data_seg_size(evfs_view_pt view, int size)
{
    int element_data_seg_size;

    if (!view) {
        return posix__makeerror(EINVAL);
    }

    element_data_seg_size = (size >= 0 ? size : (size & ~0x80000000));

    if (view->cluster.data_seg_size != element_data_seg_size) {
        view->cluster.data_seg_size = element_data_seg_size;
        return evfs_cache_write_head(view->viewid, &view->cluster);
    }

    return NSP_STATUS_SUCCESSFUL;
}

int evfs_view_get_data_seg_size(const evfs_view_pt view)
{
    if (!view) {
        return 0;
    }
    
    if (view->cluster.data_seg_size < 0) {
        return (view->cluster.data_seg_size & ~0x80000000);
    }

    return view->cluster.data_seg_size;
}

int evfs_view_get_head_cluster_id(const evfs_view_pt view)
{
    return (NULL != view) ? view->cluster.head_cluster_id : -1;
}

int evfs_view_get_viewid(const evfs_view_pt view)
{
    return (NULL != view) ? view->viewid : -1;
}

int evfs_view_get_max_pre_userseg()
{
    return __evfs_view_mgr.max_pre_userseg;
}

int evfs_view_transfer_size_to_cluster_count(int size)
{
    int quota_of_clusters_require;

    if (size <= 0 || __evfs_view_mgr.max_pre_userseg <= 0) {
        return 0;
    }
    
    quota_of_clusters_require = size / __evfs_view_mgr.max_pre_userseg;
    quota_of_clusters_require += (0 == size % __evfs_view_mgr.max_pre_userseg) ? 0 : 1;
    return quota_of_clusters_require;
}

int evfs_view_looks_like_head(const evfs_view_pt view)
{
    return (NULL != view) ? (view->cluster.data_seg_size & 0x80000000) : 0;
}
