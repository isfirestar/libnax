#pragma once

/* evfs
 * 64 bytes pre cluster is the mininum format for evfs, and 1024 bytes pre cluster is the maximum, default value is 256 bytes pre cluster
 * performance of typical format like below:
 *   64 bytes/cluster format: evfs have 81.2% disk utilization rate. within 1GB maximum file size, efficient user data length is   872,415,180 bytes
 *  128 bytes/cluster format: evfs have 90.6% disk utilization rate. within 1GB maximum file size, efficient user data length is   973,078,412 bytes
 *  256 bytes/cluster format: evfs have 95.3% disk utilization rate. within 1GB maximum file size, efficient user data length is 1,023,409,932 bytes
 *  512 bytes/cluster format: evfs have 97.6% disk utilization rate. within 1GB maximum file size, efficient user data length is 1,048,575,500 bytes
 * 1024 bytes/cluster format: evfs have 98.8% disk utilization rate. within 1GB maximum file size, efficient user data length is 1,061,157,900 bytes
 * 
 * evfs allow share entry-descriptor to any other thread, 
 *  but not permit I/O from multiple thread on the same entry-descriptor at the same time.
 * 
 * evfs (entries virtual file system) use in easy to save entries, such as some configuration or key-value data pair
 *  it's a virtual file system, so it's not a real file system, it's not a file system at all
 *  but it easy to create,delete,read,write,iterate entries during runtime, all it's behavior is like a file system
 */

#include "compiler.h"

typedef struct evfs_interator *evfs_iterator_pt;

PORTABLEAPI(nsp_status_t)                   evfs_create(const char *path, int cluster_size_format, int cluster_count_format, int count_of_cache_cluster);
PORTABLEAPI(nsp_status_t)                   evfs_open(const char *path, int count_of_cache_cluster);
PORTABLEAPI(void)                           evfs_close();

typedef struct _evfs_stat
{
    int cluster_size;
    int cluster_count;
    int cluster_idle;
    int cluster_busy;
    int entries;
} evfs_stat_t;
PORTABLEAPI(nsp_status_t)                   evfs_query_stat(evfs_stat_t *evstat);
PORTABLEAPI(float)                          evfs_query_cache_performance();

typedef int evfs_entry_handle_t;

PORTABLEAPI(evfs_entry_handle_t)            evfs_create_entry(const char *key);
PORTABLEAPI(evfs_entry_handle_t)            evfs_open_entry(int entry_id);
PORTABLEAPI(evfs_entry_handle_t)            evfs_open_entry_bykey(const char *key);
PORTABLEAPI(nsp_status_t)                   evfs_earse_entry(evfs_entry_handle_t handle);
PORTABLEAPI(void)                           evfs_close_entry(evfs_entry_handle_t handle);
PORTABLEAPI(int)                            evfs_get_entry_size(evfs_entry_handle_t handle);

PORTABLEAPI(int)                            evfs_query_entry_length(evfs_entry_handle_t handle);
PORTABLEAPI(int)                            evfs_write_entry(evfs_entry_handle_t handle, const char *data, int size);
PORTABLEAPI(int)                            evfs_read_entry(evfs_entry_handle_t handle, char *data, int size);
PORTABLEAPI(nsp_status_t)                   evfs_seek_entry_offset(evfs_entry_handle_t handle, int seek);
PORTABLEAPI(nsp_status_t)                   evfs_truncate_entry(evfs_entry_handle_t handle, int size);
PORTABLEAPI(void)                           evfs_flush_entry_buffer(evfs_entry_handle_t handle);

PORTABLEAPI(evfs_iterator_pt)               evfs_iterate_entries(evfs_iterator_pt iterator);
PORTABLEAPI(void)                           evfs_release_iterator(evfs_iterator_pt iterator);
PORTABLEAPI(int)                            evfs_read_iterator_entry(const evfs_iterator_pt iterator, char *data, int size);
PORTABLEAPI(int)                            evfs_get_iterator_entry_id(const evfs_iterator_pt iterator);
PORTABLEAPI(int)                            evfs_get_iterator_entry_size(const evfs_iterator_pt iterator);
PORTABLEAPI(const char *)                   evfs_get_iterator_entry_key(const evfs_iterator_pt iterator);
