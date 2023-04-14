#pragma once

#include "compiler.h"

#include "clist.h"

#define MAX_ENTRY_NAME_LENGTH       (32)

struct evfs_entries_stat
{
    int idle_view_count;
    int busy_view_count;
    int total_entry_count;
};

/* entries initial/uninitial proc */
extern nsp_status_t evfs_entries_create();
extern nsp_status_t evfs_entries_load();
extern void evfs_entries_uninit();

/* create a empty entry and set length as placeholder */
extern nsp_status_t evfs_entries_create_one(const char *key, int *entry_id);
extern nsp_status_t evfs_entries_open_one(int entry_id);
extern nsp_status_t evfs_entries_open_one_by_name(const char *key, int *entry_id);
extern void evfs_entries_close_one(int entry_id);

/* remove entry and move all elements from busy to free */
extern nsp_status_t evfs_entries_soft_delete(int entry_id);
extern nsp_status_t evfs_entries_hard_delete(int entry_id);
extern nsp_status_t evfs_entries_hard_delete_by_name(const char *key);
extern int evfs_entries_get_entry_id_by_name(const char *key);

/* base I/O proc for entry
 *  truncate the entry and it didn't affect the offset of entry, this method can either expand or reduce entry occupied */
extern int evfs_entries_write_data(int entry_id, const char *data, int offset, int size);
extern int evfs_entries_read_data(int entry_id, char *data, int offset, int size);
extern nsp_status_t evfs_entries_truncate(int entry_id, int size);
extern void evfs_entries_flush(int entry_id);

/* query the name of entry */
extern int evfs_entries_query_key(int entry_id, char *key);
/* query user data segment length of entry in bytes, exclude name length */
extern int evfs_entries_query_user_seg_size(int entry_id);

/* entires interation */
extern int evfs_entries_iterate(struct list_head **pos);

/* query state */
extern nsp_status_t evfs_entries_soft_stat(struct evfs_entries_stat *stat);
