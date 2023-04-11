#pragma once

/* cache.h
 *  this module provides a cache for storing data in memory
 *  the cache is implemented as a binary search tree
 *  the cache is used to store the data of the file system, every cache node are reflect a evfs cluster
 */

#include "compiler.h"
#include "avltree.h"
#include "clist.h"

#include "cluster.h"

struct evfs_cache_creator {
    int cluster_size;
    int cluster_count;
};

/* init evfs cache by specify fix cluster count, and the size of single cluster.
 * the @cluster_size indicate how many bytes evfs_cache_node::data shall be allocate, and it must equal to evfs_head_record_data::cluster_size.
 * cache_cluster_count indicate how many evfs_cache_node shall be allocate, and it must equal to or less than evfs_head_record_data::cluster_count.
 * if @cache_cluster_count equal to 0, that means the cache is disabled.
 * after success call to evfs_cache_init, the cache is empty.and caller has resposibility to destroy the cache by invoke @evfs_cache_uninit
 * @evfs_cache_add_block function add a new cache block to cache, and it will be used to store data of cluster
 */
extern nsp_status_t evfs_cache_init(const char *file, int cache_cluster_count, const struct evfs_cache_creator *creator);
extern nsp_status_t evfs_cache_add_block(int cache_cluster_count);
extern void evfs_cache_uninit();

/* read entire cluster data from cache,and store in legal output parameter @data
 * @cluster_id indicate the cluster id to be read, it must be in range [0, evfs_head_record_data::cluster_count)
 * if the cluster is not in cache, cache block will be allocated and read from disk
 * @evfs_cache_read_head function read only the head of cluster. if this block not exist, it will load entire cluster data from disk
 * @evfs_cache_read_userdata function read only the userdata of cluster. if this block not exist, it will load entire cluster data from disk
 */
extern nsp_status_t evfs_cache_read(int cluster_id, void *output, int offset, int length);
extern nsp_status_t evfs_cache_read_head(int cluster_id, evfs_cluster_pt clusterptr);
extern nsp_status_t evfs_cache_read_userdata(int cluster_id, void *output, int offset, int length);

/* directly read entry cluster data from harddisk.
 * these operations are not affect the cache but merge the IO request to the uniform thread
 */
extern nsp_status_t evfs_cache_read_directly(int cluster_id, void *output);
extern nsp_status_t evfs_cache_read_head_directly(int cluster_id, evfs_cluster_pt clusterptr);
                    
/* write entire cluster data to cache
 * @cluster_id indicate the cluster id to be write, it must be in range [0, evfs_head_record_data::cluster_count)
 * if the cluster is not in cache, cache block will be allocated
 * @evfs_cache_write_head function read only the head of cluster. if this block not exist, it will load entire cluster data from disk
 * @evfs_cache_write_userdata function read only the userdata of cluster. if this block not exist, it will load entire cluster data from disk
 */
extern nsp_status_t evfs_cache_write(int cluster_id, const void *input, int offset, int length);
extern nsp_status_t evfs_cache_write_head(int cluster_id, const evfs_cluster_pt clusterptr);
extern nsp_status_t evfs_cache_write_userdata(int cluster_id, const void *input, int offset, int length);

/* directly write entry cluster data to harddisk.
 * these operations are not affect the cache but merge the IO request to the uniform thread
 */
extern nsp_status_t evfs_cache_write_directly(int cluster_id, const void *input);

/* flush all dirty cache blocks to harddisk */
extern void evfs_cache_flush(int no_wait);
extern void evfs_cache_flush_block(int cluster_id, int no_wait);

/* caculate the rate of hit */
extern float evfs_cache_hit_rate();

/* wrap hard state */
extern nsp_status_t evfs_cache_hard_state(struct evfs_cache_creator *creator);
