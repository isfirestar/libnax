#pragma once

#include "compiler.h"

typedef struct evfs_view_node *evfs_view_pt;
typedef void (*on_view_loaded_t)(evfs_view_pt view);

extern nsp_status_t evfs_view_create();
extern nsp_status_t evfs_view_load(on_view_loaded_t on_loaded);
extern nsp_status_t evfs_view_expand();
extern void evfs_view_uninit();

extern evfs_view_pt evfs_view_acquire_idle();
extern nsp_status_t evfs_view_acquire_idle_more(int amount, evfs_view_pt *views);

extern nsp_status_t evfs_view_move_to_idle(evfs_view_pt view);
extern evfs_view_pt evfs_view_acquire_idle();

extern void evfs_view_flush(evfs_view_pt view);

/* view read */
extern nsp_status_t evfs_view_read_userdata(const evfs_view_pt view, void *buffer, int offset, int length);

/* view write */
extern nsp_status_t evfs_view_write_head(evfs_view_pt view);
extern nsp_status_t evfs_view_write_userdata(const evfs_view_pt view, const void *buffer, int offset, int length);

/* change or get the next cluster id of view */
extern nsp_status_t evfs_view_set_next(evfs_view_pt view, const evfs_view_pt next_view);
extern int evfs_view_get_next_cluster_id(const evfs_view_pt view);

/* change or get the user data seg size of view, in get function */
extern nsp_status_t evfs_view_set_head_data_seg_size(evfs_view_pt view, int size);
extern nsp_status_t evfs_view_set_element_data_seg_size(evfs_view_pt view, int size);
extern int evfs_view_get_data_seg_size(const evfs_view_pt view);

/* change or get the head cluster id of view */
extern nsp_status_t evfs_view_set_head(evfs_view_pt view, const evfs_view_pt head_view);
extern int evfs_view_get_head_cluster_id(const evfs_view_pt view);

/* get cluster id of view */
extern int evfs_view_get_viewid(const evfs_view_pt view);

/* get view manager count status */
extern void evfs_view_get_count(int *freeview, int *busyview);

/* tunnel cluster property */
extern int evfs_view_get_max_pre_userseg();

/* exchange bytes to cluster count */
extern int evfs_view_transfer_size_to_cluster_count(int size);

/* estimate the view(cluster) is a head of entry */
extern int evfs_view_looks_like_head(const evfs_view_pt view);

