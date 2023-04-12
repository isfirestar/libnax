#include <gtest/gtest.h>
#include <cstring>

#include "evfs.h"

// create and format a file as evfs target, and than open it again, both of these operation MUST success
TEST(DoCreateEvfs, CreateEvfs)
{
    nsp_status_t status;

    // canonical create and format, MUST success
    status = evfs_create("./evfstest.db", 128, 100, 85);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();

    // create with error parameter, which the cluster size is not a multiple of 2, MUST fail
    status = evfs_create("./evfstest.db", 127, 100, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with error parameter, which the cluster size large than MAXIMUM_CLUSTER_SIZE or less than MINIMUM_CLUSTER_SIZE, MUST fail
    status = evfs_create("./evfstest.db", 4097, 100, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_create("./evfstest.db", 31, 100, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with error parameter, which cause the target file large than 1GB, MUST fail
    status = evfs_create("./evfstest.db", 128, 10000000, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // create with zero cache size, MUST success
    status = evfs_create("./evfstest.db", 128, 100, 0);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();

    // double create, MUST fail
    status = evfs_create("./evfstest.db", 128, 100, 85);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_create("./evfstest.db", 128, 100, 85);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();
}

// open a exist evfs file, and than close it, both of these operation MUST success
TEST(DoOpenEvfs, OpenEvfs)
{
    nsp_status_t status;

    // canonical open, MUST success
    status = evfs_open("./evfstest.db", 100);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();

    // open with error parameter, which the target file is not exist, MUST fail
    status = evfs_open("./evfstest.db.notexist", 100);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);

    // double open, MUST fail
    status = evfs_open("./evfstest.db", 100);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    status = evfs_open("./evfstest.db", 100);
    EXPECT_NE(status, NSP_STATUS_SUCCESSFUL);
    evfs_close();
}

// create a new file and some entries in it, and than test the iteration function
TEST(DoCreateIterate, CreateIterate)
{
    nsp_status_t status;

    // canonical create and format, MUST success
    status = evfs_create("./evfstest.db", 128, 100, 85);
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
    status = evfs_create("./evfstest.db", 128, 100, 85);
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
