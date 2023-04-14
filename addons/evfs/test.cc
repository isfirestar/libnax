#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

#include "evfs.h"

// create and format a file as evfs target, and than open it again, both of these operation MUST success
TEST(DoCreateEvfs, CreateEvfs)
{
    nsp_status_t status;

    // canonical create and format, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));
    evfs_close();

    // create with error parameter, which the cluster size is not a multiple of 2, MUST fail
    status = evfs_create("./evfstest.db", 127, 10, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with error parameter, which the cluster size large than MAXIMUM_CLUSTER_SIZE or less than MINIMUM_CLUSTER_SIZE, MUST fail
    status = evfs_create("./evfstest.db", 4097, 10, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_create("./evfstest.db", 31, 10, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with error parameter, which cause the target file large than 1GB, MUST fail
    status = evfs_create("./evfstest.db", 128, 10000000, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with zero cache size, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    evfs_close();

    // double create, MUST fail
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();
}

// open a exist evfs file, and than close it, both of these operation MUST success
TEST(DoOpenEvfs, OpenEvfs)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_open("./evfstest.db", 85);
    EXPECT_TRUE(NSP_SUCCESS(status));
    evfs_close();

    // open with error parameter, which the target file is not exist, MUST fail
    status = evfs_open("./evfstest.db.notexist", 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // double open, MUST fail
    status = evfs_open("./evfstest.db", 85);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = evfs_open("./evfstest.db", 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();
}

// create a new file and some entries in it, and than test the iteration function
TEST(DoCreateIterate, CreateIterate)
{
    nsp_status_t status;

    // canonical create and format, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create 5 new entries, MUST success
    evfs_entry_handle_t handle[5];
    for (int i = 0; i < 5; i++)
    {
        char name[32];
        sprintf(name, "test%d", i);
        handle[i] = evfs_create_entry(name);
        EXPECT_GT(handle[i], 0);
        evfs_close_entry(handle[i]);
    }

    // iterate the entries, MUST success
    int n = 0;
    evfs_iterator_pt iterator = evfs_iterate_entries(NULL);
    while (iterator) {
        int entry_id = evfs_get_iterator_entry_id(iterator);
        EXPECT_GT(entry_id, 0);
        const char* entry_name = evfs_get_iterator_entry_key(iterator);
        EXPECT_TRUE(entry_name != NULL);
        int entry_size = evfs_get_iterator_entry_size(iterator);
        EXPECT_GE(entry_size, 0);
        iterator = evfs_iterate_entries(iterator);
        n++;
    }
    EXPECT_EQ(n, 5);

    // close the evfs file
    evfs_close();
}

// create a new file and some entries in it, and than test the read/write function
TEST(DoCreateReadWrite, CreateReadWrite)
{
    nsp_status_t status;

    // canonical create and format, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create a new entries, MUST success
    evfs_entry_handle_t handle = evfs_create_entry("test.txt");
    EXPECT_GT(handle, 0);

    // wrtie data to the entry, return value MUST be equal to the length of data
    const char* data1 = "hello world";
    int written = evfs_write_entry(handle, data1, strlen(data1));
    EXPECT_EQ(written, strlen(data1));

    // write data step over cluster boundary, return value MUST be equal to the length of data
    const char* data2 = "step over cluster boundary";
    status = evfs_seek_entry_offset(handle, 80);
    EXPECT_TRUE(NSP_SUCCESS(status));
    written = evfs_write_entry(handle, data2, strlen(data2));
    EXPECT_EQ(written, strlen(data2));

    // read data from the entry with offset 0 and length of data1, return value MUST be equal to the length of data
    // output data MUST be equal to data1
    char buffer[500];
    status = evfs_seek_entry_offset(handle, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    int read = evfs_read_entry(handle, buffer, strlen(data1));
    EXPECT_EQ(read, strlen(data1));
    EXPECT_EQ(memcmp(buffer, data1, strlen(data1)), 0);

    // seek offset to 80, read data from the entry with offset 80 and length of data2, return value MUST be equal to the length of data
    // output data MUST be equal to data2
    status = evfs_seek_entry_offset(handle, 80);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer, strlen(data2));
    EXPECT_EQ(read, strlen(data2));
    EXPECT_EQ(memcmp(buffer, data2, strlen(data2)), 0);

    // current total size of this entry MUST be equal to the length of data2 + 80
    int size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, strlen(data2) + 80);

    // allocate buffer with @size bytes and read all data from the entry, return value MUST be equal size
    char* buffer2 = new (std::nothrow) char[size];
    EXPECT_TRUE(buffer2 != NULL);
    status = evfs_seek_entry_offset(handle, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer2, size);
    EXPECT_EQ(read, size);
    EXPECT_EQ(memcmp(buffer2, data1, strlen(data1)), 0);
    EXPECT_EQ(memcmp(buffer2 + 80, data2, strlen(data2)), 0);
    delete [] buffer2;

    // close the evfs file
    evfs_close();
}

// open a exist file and some entries in it, and than test the read/write function
TEST(DoOpenReadWrite, OpenReadWrite)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_open("./evfstest.db", 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // entry "test.txt" shall be exist, MUST success
    evfs_entry_handle_t handle = evfs_open_entry_by_key("test.txt");
    EXPECT_GT(handle, 0);

    // query length and read entire data, MUST success
    int size = evfs_get_entry_size(handle);
    EXPECT_GT(size, 0);
    char* buffer = new (std::nothrow) char[size];
    EXPECT_TRUE(buffer != NULL);
    int read = evfs_read_entry(handle, buffer, size);
    EXPECT_EQ(read, size);
    delete [] buffer;

    // create a new entry and write data into it, MUST success
    // this is a boundary test, the data size is equal to the cluster size
    // because there are 32 bytes store key of the entry, so we fill 128 - 32 - 12 = 84 bytes to the first cluster and 128 bytes to the second cluster
    static const char* full = "this string is a simple cluster data demo, it's 128 bytes long, this block are available for a evfs tester, create in 2023-04-14";
    size_t lenfull = strlen(full);
    EXPECT_EQ(lenfull, 128);

    const char *the_first_cluster_remain = "there are 32 bytes remian       ";
    size_t len_the_first_cluster_remain = strlen(the_first_cluster_remain);
    EXPECT_EQ(len_the_first_cluster_remain, 32);

    handle = evfs_create_entry("boundary.txt");
    EXPECT_GT(handle, 0);
    int written = evfs_write_entry(handle, the_first_cluster_remain, len_the_first_cluster_remain);
    EXPECT_EQ(written, len_the_first_cluster_remain);
    written = evfs_write_entry(handle, full, lenfull);
    EXPECT_EQ(written, lenfull);
    // query length and read entire data, MUST success
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, lenfull + len_the_first_cluster_remain);
    buffer = new (std::nothrow) char[size];
    EXPECT_TRUE(buffer != NULL);
    status = evfs_seek_entry_offset(handle, 0);
    read = evfs_read_entry(handle, buffer, size);
    EXPECT_EQ(read, size);
    // seek offset to the tail of the first clsuter -- 84 bytes, and read data from the second cluster, it MUST be equal to the "full"
    status = evfs_seek_entry_offset(handle, len_the_first_cluster_remain);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer, lenfull);
    EXPECT_EQ(read, lenfull);
    EXPECT_EQ(memcmp(buffer, full, lenfull), 0);
    // seek offset to the tail of the second cluster, and read data, return length MUST be 0
    status = evfs_seek_entry_offset(handle, lenfull + len_the_first_cluster_remain);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer, lenfull);
    EXPECT_EQ(read, 0);
    // seek offset to 0 and read the first cluster, it MUST be equal to the_first_cluster_remain
    status = evfs_seek_entry_offset(handle, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer, len_the_first_cluster_remain);
    EXPECT_EQ(read, len_the_first_cluster_remain);
    EXPECT_EQ(memcmp(buffer, the_first_cluster_remain, len_the_first_cluster_remain), 0);
    // seek offset to the position which large than total size of the entry, and read data, return length MUST be 0
    status = evfs_seek_entry_offset(handle, lenfull + len_the_first_cluster_remain + 1);
    EXPECT_TRUE(NSP_SUCCESS(status));
    read = evfs_read_entry(handle, buffer, lenfull);
    EXPECT_EQ(read, 0);
    // seek offset to a negative value, MUST fail
    status = evfs_seek_entry_offset(handle, -1);
    EXPECT_FALSE(NSP_SUCCESS(status));
    delete [] buffer;


    // read file /etc/shadow(almost 3KB), and put it into a new entry "shadow.txt", MUST success
    FILE* fp = fopen("/usr/include/stdio.h", "rb");
    EXPECT_TRUE(fp != NULL);
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buffer = new (std::nothrow) char[size];
    EXPECT_TRUE(buffer != NULL);
    read = fread(buffer, 1, size, fp);
    EXPECT_EQ(read, size);
    fclose(fp);
    handle = evfs_create_entry("stdio.h");
    EXPECT_GT(handle, 0);
    written = evfs_write_entry(handle, buffer, size);
    EXPECT_EQ(written, size);
    delete [] buffer;

    // close the evfs file
    evfs_close();
}

// open exist file and test entry create and delete functions
TEST(DoOpenCreateDelete, OpenCreateDelete)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create a new entry and close it immediately, after that, open entry again and close it again, MUST success
    // at last, delete the entry, MUST success
    evfs_entry_handle_t handle = evfs_create_entry("xixihaha.txt");
    EXPECT_GT(handle, 0);
    evfs_close_entry(handle);

    handle = evfs_open_entry_by_key("xixihaha.txt");
    EXPECT_GT(handle, 0);
    evfs_close_entry(handle);

    status = evfs_earse_entry_by_key("xixihaha.txt");
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create a new entry and write some data to it, after that, close and delete the entry, MUST success
    handle = evfs_create_entry("xixihaha.txt");
    EXPECT_GT(handle, 0);
    const char* data = "hello world";
    int written = evfs_write_entry(handle, data, strlen(data));
    EXPECT_EQ(written, strlen(data));
    evfs_close_entry(handle);

    status = evfs_earse_entry_by_key("xixihaha.txt");
    EXPECT_TRUE(NSP_SUCCESS(status));

    // close the evfs file
    evfs_close();
}

// test truncate function
TEST(DoCreateTruncate, CreateTruncate)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create a entry and write more than 1 cluster data to it, MUST success
    evfs_entry_handle_t handle = evfs_create_entry("xixihaha.txt");
    EXPECT_GT(handle, 0);
    status = evfs_seek_entry_offset(handle, 300);
    EXPECT_TRUE(NSP_SUCCESS(status));
    const char* data = "hello world";
    int written = evfs_write_entry(handle, data, strlen(data));
    EXPECT_EQ(written, strlen(data));

    // length now must be 300 + strlen(data)
    int size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 300 + strlen(data));

    // truncate entry let it decrease to 20 bytes, MUST success
    // after truncate, length must be 20
    status = evfs_truncate_entry(handle, 20);
    EXPECT_TRUE(NSP_SUCCESS(status));
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 20);

    // truncate entry let it increase to 100 bytes, MUST success
    // after truncate, length must be 100
    status = evfs_truncate_entry(handle, 100);
    EXPECT_TRUE(NSP_SUCCESS(status));
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 100);

    // close the evfs file
    evfs_close();
}

// test the filesystem state function and cache modify function
TEST(DoTestCacheAndStateQuery, CacheAndStateQuery)
{
    nsp_status_t status;
    evfs_stat_t stat;

    // canonical open, MUST success
    status = evfs_create("./evfstest.db", 128, 100, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // query the filesystem state, MUST success
    // compare the state with the state we set when create the filesystem
    status = evfs_query_stat(&stat);
    EXPECT_TRUE(NSP_SUCCESS(status));
    EXPECT_EQ(stat.cluster_size, 128);
    EXPECT_EQ(stat.cluster_count, 99);
    EXPECT_EQ(stat.cluster_idle, 99);
    EXPECT_EQ(stat.entry_count, 0);
    EXPECT_EQ(stat.cluster_busy, 0);
    EXPECT_EQ(stat.cache_block_num, 85);

    // add cache node and then query the state again, MUST success
    // compare the state with the state we set when create the filesystem
    status = evfs_set_cache_block_num(120);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = evfs_query_stat(&stat);
    EXPECT_EQ(stat.cache_block_num, 120);

    // decrease cache node number and query again
    status = evfs_set_cache_block_num(80);
    EXPECT_TRUE(NSP_SUCCESS(status));
    status = evfs_query_stat(&stat);
    EXPECT_EQ(stat.cache_block_num, 80);

    // close the evfs file
    evfs_close();
}

// test clusters recycle and resue function
// we create a file and write some data to it, then delete it
// after that, we create a new file and write some data to it
// we verify the data consistency
TEST(DoTestClusterRecycleAndReuse, TestClusterRecycleAndReuse)
{
    char *buffer;

    nsp_status_t status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_TRUE(NSP_SUCCESS(status));

    // create a entry and wirte data more than 10 clusters, ensure that all clusters in file have been occupied by this entry
    evfs_entry_handle_t handle = evfs_create_entry("xixihaha.txt");
    EXPECT_GT(handle, 0);
    int size = 128 * 11;
    buffer = new char[size];
    memset(buffer, 'a', size);
    int written = evfs_write_entry(handle, buffer, size);
    EXPECT_EQ(written, size);
    delete [] buffer;
    buffer = nullptr;
    evfs_close_entry(handle);

    // now delete the entry, meanwhile, all clusters in file have been recycled
    // after delete, this entry shall not be able to open again
    status = evfs_earse_entry_by_key("xixihaha.txt");
    EXPECT_TRUE(NSP_SUCCESS(status));
    handle = evfs_open_entry_by_key("xixihaha.txt");
    EXPECT_EQ(handle, -1);

    // create another entry, seek to the offset where less than 10 clusters, but didn't write data to it
    // at this moment, the entry size shall be the offset where we seek to
    // after that, we seek the offset to zero and read data from entry, the data shall be zero
    handle = evfs_create_entry("balabala.txt");
    EXPECT_GT(handle, 0);
    status = evfs_seek_entry_offset(handle, 128 * 5);
    EXPECT_TRUE(NSP_SUCCESS(status));
    size = evfs_write_entry(handle, "hello world", strlen("hello world"));
    EXPECT_EQ(size, strlen("hello world"));
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 128 * 5 + strlen("hello world"));
    status = evfs_seek_entry_offset(handle, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 128 * 5 + strlen("hello world"));
    buffer = new char[128 * 5];
    ASSERT_TRUE(buffer != nullptr);
    int read = evfs_read_entry(handle, buffer, 128 * 5);
    EXPECT_EQ(read, 128 * 5);
    int *p = (int *)buffer;
    for (int i = 0; i < 128 * 5 / 4; i++)
    {
        EXPECT_EQ(p[i], 0);
    }
    delete [] buffer;
    buffer = nullptr;
    evfs_close_entry(handle);

    // close the evfs file
    evfs_close();
}
