#pragma once

#include "compiler.h"

/* evfs split file to many of cluster, every cluster has it's fix length, 
 *  the cluster size must be power of 2, and the cluster size must be between 32 and 4096 bytes
 *   the total file size must be less than 1GB
*/

#define MINIMUM_CLUSTER_SIZE    (32)
#define DEFAULT_CLUSTER_SIZE    (128)
#define MAXIMUM_CLUSTER_SIZE    (4096)

#define MAXIMUM_FILE_SIZE       (1 << 30)   /* 1GB */
#define EVFS_MAGIC              ((int)'sfvE')

enum evfs_ready_stat {
    kEvmgrNotReady = 0,
    kEvmgrInitializing = 1,
    kEvmgrUninitializing,
    kEvmgrReady,
    kEvmgrClosing,
    kEvmgrBusy,
};

#pragma pack(push,1)

struct evfs_cluster
{
    //int cluster_id;
    int data_seg_size; /* when the cluster is the head of entry, this field indicate the size of user data (in bytes) 
                            particularly, if this field set to -1(CLUSTER_PLACEHOLDER), this case indicate cluster has been allocated with zero data length*/
    int next_cluster_id;
    int head_cluster_id; /* whe a entry consist of more than one cluster, this field are pointer to the head cluster-id of this entry */
    unsigned char data[0];
};

struct evfs_head_record_data
{
    int magic;
    int cluster_size;               /* size of one cluster in bytes */
    int cluster_count;              /* total count of clusters */
    int expand_cluster_count;       /* count of clusters for each extend request */
    union {
        int reserved[16];               /* reserve space */
    };
};

#pragma pack(pop)

typedef struct evfs_cluster *evfs_cluster_pt;

#define evfs_cluster_userdata(clusterptr)           ((clusterptr)->data)
#define evfs_cluster_size_legal(size)               ((size) >= MINIMUM_CLUSTER_SIZE && (size) <= DEFAULT_CLUSTER_SIZE && is_powerof_2((size)))
#define evfs_cluster_looks_like_busy(clusterptr)    ((0 != (clusterptr)->data_seg_size) || (clusterptr)->head_cluster_id > 0)

/* create a new filesystem and dump the file to harddisk,
    in this proc, we only build the 1st cluster, other cluster acquire calling thread to initialize after this proc success returned */
extern nsp_status_t evfs_create_filesystem(const char *file, int cluster_size_format, int cluster_count);
extern nsp_status_t evfs_open_filesystem(const char *file);
extern void evfs_close_filesystem();
extern int evfs_expand_filesystem(int *expanded_head_cluster_id);

/* read/write request on harddisk in cluster */
extern nsp_status_t evfs_cluster_write(int cluster_id, const void *buffer);
extern nsp_status_t evfs_cluster_read(int cluster_id, void *buffer);
extern nsp_status_t evfs_cluster_read_head(int cluster_id, struct evfs_cluster *clusterptr);

/* manager value query */
extern int evfs_cluster_get_size();
extern int evfs_cluster_get_usable_count();
extern int evfs_cluster_get_max_pre_userseg();

/* cluster data memory block */
extern evfs_cluster_pt evfs_allocate_cluster_memory(const evfs_cluster_pt clusterptr);
extern void evfs_free_cluster_memory(evfs_cluster_pt clusterptr);