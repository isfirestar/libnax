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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();

    // double create, MUST fail
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();

    // open with error parameter, which the target file is not exist, MUST fail
    status = evfs_open("./evfstest.db.notexist", 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // double open, MUST fail
    status = evfs_open("./evfstest.db", 85);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    written = evfs_write_entry(handle, data2, strlen(data2));
    EXPECT_EQ(written, strlen(data2));

    // read data from the entry with offset 0 and length of data1, return value MUST be equal to the length of data
    // output data MUST be equal to data1
    char buffer[500];
    status = evfs_seek_entry_offset(handle, 0);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    int read = evfs_read_entry(handle, buffer, strlen(data1));
    EXPECT_EQ(read, strlen(data1));
    EXPECT_EQ(memcmp(buffer, data1, strlen(data1)), 0);

    // seek offset to 80, read data from the entry with offset 80 and length of data2, return value MUST be equal to the length of data
    // output data MUST be equal to data2
    status = evfs_seek_entry_offset(handle, 80);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // entry "test.txt" shall be exist, MUST success
    evfs_entry_handle_t handle = evfs_open_entry_bykey("test.txt");
    EXPECT_GT(handle, 0);

    // query length and read entire data, MUST success
    int size = evfs_get_entry_size(handle);
    EXPECT_GT(size, 0);
    char* buffer = new (std::nothrow) char[size];
    EXPECT_TRUE(buffer != NULL);
    int read = evfs_read_entry(handle, buffer, size);
    EXPECT_EQ(read, size);
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
    int written = evfs_write_entry(handle, buffer, size);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // create a new entry and close it immediately, after that, open entry again and close it again, MUST success
    // at last, delete the entry, MUST success
    evfs_entry_handle_t handle = evfs_create_entry("didadi.txt");
    EXPECT_GT(handle, 0);
    evfs_close_entry(handle);

    handle = evfs_open_entry_bykey("didadi.txt");
    EXPECT_GT(handle, 0);
    evfs_close_entry(handle);

    status = evfs_earse_entry_by_name("didadi.txt");
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // create a new entry and write some data to it, after that, close and delete the entry, MUST success
    handle = evfs_create_entry("didadi.txt");
    EXPECT_GT(handle, 0);
    const char* data = "hello world";
    int written = evfs_write_entry(handle, data, strlen(data));
    EXPECT_EQ(written, strlen(data));
    evfs_close_entry(handle);

    status = evfs_earse_entry_by_name("didadi.txt");
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // close the evfs file
    evfs_close();
}

// test truncate function
TEST(DoCreateTruncate, CreateTruncate)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_create("./evfstest.db", 128, 10, 85);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // create a entry and write more than 1 cluster data to it, MUST success
    evfs_entry_handle_t handle = evfs_create_entry("didadi.txt");
    EXPECT_GT(handle, 0);
    status = evfs_seek_entry_offset(handle, 300);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    const char* data = "hello world";
    int written = evfs_write_entry(handle, data, strlen(data));
    EXPECT_EQ(written, strlen(data));

    // length now must be 300 + strlen(data)
    int size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 300 + strlen(data));

    // truncate entry let it decrease to 20 bytes, MUST success
    // after truncate, length must be 20
    status = evfs_truncate_entry(handle, 20);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    size = evfs_get_entry_size(handle);
    EXPECT_EQ(size, 20);

    // truncate entry let it increase to 100 bytes, MUST success
    // after truncate, length must be 100
    status = evfs_truncate_entry(handle, 100);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
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
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);

    // query the filesystem state, MUST success
    // compare the state with the state we set when create the filesystem
    status = evfs_query_stat(&stat);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_EQ(stat.cluster_size, 128);
    EXPECT_EQ(stat.cluster_count, 99);
    EXPECT_EQ(stat.cluster_idle, 99);
    EXPECT_EQ(stat.entry_count, 0);
    EXPECT_EQ(stat.cluster_busy, 0);
    EXPECT_EQ(stat.cache_block_num, 85);

    // add cache node and then query the state again, MUST success
    // compare the state with the state we set when create the filesystem
    status = evfs_set_cache_block_num(120);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_query_stat(&stat);
    EXPECT_EQ(stat.cache_block_num, 120);

    // decrease cache node number and query again
    status = evfs_set_cache_block_num(80);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_query_stat(&stat);
    EXPECT_EQ(stat.cache_block_num, 80);

    // close the evfs file
    evfs_close();
}
