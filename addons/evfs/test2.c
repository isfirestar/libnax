#include "evfs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "ifos.h"

// parse command line
// every command line is split by space
// words which start with '"' will be treated as a whole string
// output parameter @target MUST allocate by caller, @max parameter indicate the max element count of @target
// on success, return the element count of @target, otherwise return -1
int cmdparse(char *pcmd, char **target, int max)
{
    int count = 0;
    char *cursor = pcmd;
    char *start = NULL;
    char *end = NULL;

    while (cursor && count < max) {
        if (0 == *cursor) {
            break;
        }

        if (isspace(*cursor)) {
            cursor++;
            continue;
        }

        if ('"' == *cursor) {
            start = cursor + 1;
            end = strchr(start, '"');
            if (NULL == end) {
                return -1;
            }
            *end = 0;
            cursor = end + 1;
        } else {
            start = cursor;
            end = strchr(start, ' ');
            if (NULL == end) {
                end = start + strlen(start);
            }
            *end = 0;
            cursor = end + 1;
        }

        target[count] = start;
        count++;
    }

    return count;
}

char *display_prompt_and_waitting_for_input(char *command, int len)
{
    int rdcb;

    do {
        write(1, "> ", 2);

        rdcb = read(0, command, len);
        if (rdcb <= 0) {
            return NULL;
        }

        command[rdcb - 1] = 0; /* eliminate \n symbol */
    } while(0 == command[0]);
    return command;
}

void showhelp()
{
    static const char helplist[] = "openfs\n"
                                    "createfs\n"
                                    "ls\n"
                                    "exit\n"
                                    "read\n"
                                    "open\n"
                                    "close\n"
                                    "new\n"
                                    "flush\n"
                                    "hit\n"
                                    "import\n"
                                    "export\n"
                                    "delete\n";
    printf("%s\n", helplist);
}

void evfs_test_iterate_all_entries()
{
    evfs_iterator_pt iterator;
    int user_seg_size, entry_id;
    const char *entry_key;

    iterator = NULL;
    while (NULL != (iterator = evfs_iterate_entries(iterator))) {
        entry_id = evfs_get_iterator_entry_id(iterator);
        user_seg_size = evfs_get_iterator_entry_size(iterator);
        entry_key = evfs_get_iterator_entry_key(iterator);
        printf("%d\t%d\t%s\n", entry_id, user_seg_size, entry_key);
    }
}

void evfs_test_read_entry_data(evfs_entry_handle_t handle, char *pcmd)
{
    char *target[2];
    int count, off, len, maxlen, readlen;
    nsp_status_t status;
    char *buffer;

    count = cmdparse(pcmd, target, 2);
    if (count >= 2) {
        off = atoi(target[1]);
        len = atoi(target[0]);
    } else if (count == 1) {
        off = atoi(target[1]);
        len = 0;
    } else {
        off = 0;
        len = 0;
    }

    maxlen = evfs_query_entry_length(handle);
    if (maxlen <= 0) {
        printf("illegal size? %d\n", maxlen);
        return;
    }
    readlen = ((0 == len) ? maxlen : min(maxlen, len));

    status = evfs_seek_entry_offset(handle, off);
    if (!NSP_SUCCESS(status)) {
        printf("failed seek entry offset to %d\n", off);
        return;
    }

    buffer = (char *)malloc(readlen + 1);
    if (!buffer) {
        return;
    }
    buffer[readlen] = 0;

    count = evfs_read_entry(handle, buffer, readlen);
    if (count > 0 ) {
        printf("there are %d bytes reading from entry\n", count);
        printf("%s\n", buffer);
    } else {
        printf("failed read entry,status:%d\n", count);
    }
    free(buffer);
 }

 void evfs_test_write_entry_data(evfs_entry_handle_t handle, char *pcmd)
 {
    char *target[2];
    int off, count;
    nsp_status_t status;
    size_t bufferlen;
    char *buffer;

    count = cmdparse(pcmd, target, 2);
    if (count < 1) {
        printf("usage : evfs write [data][offset] \n");
        return;
    }

    if (count >= 2) {
        off = atoi(target[1]);
        status = evfs_seek_entry_offset(handle, off);
        if (!NSP_SUCCESS(status)) {
            printf("failed seek entry offset to %d\n", off);
            return;
        }
    }

    bufferlen = strlen(target[0]);
    buffer = (char *)malloc(bufferlen);
    if (!buffer) {
        return;
    }
    memcpy(buffer, target[0], bufferlen);
    count = evfs_write_entry(handle, buffer, bufferlen);
    if (count > 0) {
        printf("there are %d bytes written to entry\n",count);
    } else {
        printf("failed write entry,status:%d\n", count);
    }
    free(buffer);
 }

evfs_entry_handle_t evfs_test_open_entry(const char *pcmd)
{
    const char *cursor;
    evfs_entry_handle_t handle;

    cursor = pcmd;
    while (0x20 == *cursor && 0 != *cursor) {
        cursor++;
    }

    handle = evfs_open_entry_bykey(cursor);
    if (handle > 0) {
        printf("open entry [%s] ok, handle is: %d\n", cursor, handle);
    } else {
        printf("failed open entry [%s]\n", cursor);
    }
    return handle;
}

evfs_entry_handle_t evfs_test_new_entry(const char *pcmd)
{
    const char *cursor;
    evfs_entry_handle_t handle;

    cursor = pcmd;
    while (0x20 == *cursor && 0 != *cursor) {
        cursor++;
    }

    handle = evfs_create_entry(cursor);
    if (handle > 0) {
        printf("new entry [%s] ok, handle is: %d\n", cursor, handle);
    } else {
        printf("failed new entry [%s]\n", cursor);
    }
    return handle;
}

void evfs_test_import_file_to_entry(char *pcmd, evfs_entry_handle_t handle)
{
    int count;
    char *filename;
    file_descriptor_t fd;
    nsp_status_t status;
    int64_t fsize;
    static const int max_file_size = 10 << 20;
    char *buffer;

    count = cmdparse(pcmd, &filename, 1);
    if (count < 1) {
        printf("usage : evfs import [file]\n");
        return;
    }

    // open the existing file
    status = ifos_file_open(filename, FF_RDACCESS | FF_OPEN_EXISTING, 0644, &fd);
    if (!NSP_SUCCESS(status)) {
        printf("failed open file %s with error:%ld\n", filename, status);
        return;
    }

    buffer = NULL;
    fsize = ifos_file_fgetsize(fd);
    do {
        if (fsize < 0 || fsize > max_file_size) {
            printf("failed get file size for file %s\n", filename);
            break;
        }

        buffer = (char *)malloc(fsize);
        if (!buffer) {
            printf("failed allocate memory for file %s\n", filename);
            break;
        }
        memset(buffer, 0, fsize);

        status = ifos_file_read(fd, buffer, fsize);
        if (status != fsize) {
            printf("failed read file %s with error:%ld\n", filename, status);
            break;
        }

        // seek to the beginning of the entry
        status = evfs_seek_entry_offset(handle, 0);
        if (!NSP_SUCCESS(status)) {
            printf("failed seek entry offset to 0\n");
            break;
        }

        status = evfs_write_entry(handle, buffer, fsize);
        if (status != fsize) {
            printf("failed write entry with error:%ld\n", status);
            break;
        }

        printf("import file %s to entry ok, %ld bytes written\n", filename, status);
    } while (0);

    if (buffer) {
        free(buffer);
    }
    ifos_file_close(fd);
}

void evfs_test_export_entry_to_file(char *pcmd, evfs_entry_handle_t handle)
{
    int count;
    char *filename;
    file_descriptor_t fd;
    nsp_status_t status;
    int64_t fsize;
    static const int max_file_size = 10 << 20;
    char *buffer;

    count = cmdparse(pcmd, &filename, 1);
    if (count < 1) {
        printf("usage : evfs export [file]\n");
        return;
    }

    // open the existing file
    status = ifos_file_open(filename, FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &fd);
    if (!NSP_SUCCESS(status)) {
        printf("failed open file %s with error:%ld\n", filename, status);
        return;
    }

    buffer = NULL;
    fsize = evfs_get_entry_size(handle);
    do {
        if (fsize < 0 || fsize > max_file_size) {
            printf("failed get file size for file %s\n", filename);
            break;
        }

        buffer = (char *)malloc(fsize);
        if (!buffer) {
            printf("failed allocate memory for file %s\n", filename);
            break;
        }
        memset(buffer, 0, fsize);

        // seek to the beginning of the entry
        status = evfs_seek_entry_offset(handle, 0);
        if (!NSP_SUCCESS(status)) {
            printf("failed seek entry offset to 0\n");
            break;
        }

        status = evfs_read_entry(handle, buffer, fsize);
        if (status != fsize) {
            printf("failed read entry with error:%ld", status);
            break;
        }

        // write to target file
        status = ifos_file_write(fd, buffer, fsize);
        if (status != fsize) {
            printf("failed write file with error:%ld", status);
            break;
        }
    } while (0);

    // always close file descriptor
    ifos_file_close(fd);
}

void evfs_test_delete_entry_by_name(char *pcmd)
{
    int count;
    char *target;

    count = cmdparse(pcmd, &target, 1);
    if (count < 1) {
        printf("usage : evfs delete [entry name]\n");
        return;
    }
    evfs_earse_entry_by_name(target);
}

int main(int argc, char **argv)
{
    char file[255], command[128], *pcmd;
    nsp_status_t status;
    int is_opened;
    evfs_entry_handle_t handle;

    if (argc < 2) {
        printf("usage : evfs [file]\n");
        return 1;
    }

    handle = -1;
    is_opened = 0;
    strcpy(file, argv[1]);

    while (NULL != (pcmd = display_prompt_and_waitting_for_input(command, sizeof(command)))) {
        if (0 == strncmp(pcmd, "openfs", 6)) {
            status = evfs_open(file, 8);
            if (!NSP_SUCCESS(status)) {
                printf("open evfs failed with code:%ld\n", status);
                break;
            }
            printf("open file %s ok.\n", file);
            is_opened = 1;
            continue;
        }

        if (0 == strncmp(pcmd, "createfs", 8)) {
            status = evfs_create(file, 128, 16, 8);
            if (!NSP_SUCCESS(status)) {
                printf("create evfs failed with code:%ld\n", status);
                break;
            }
            printf("create file %s ok.\n", file);
            is_opened = 1;
            continue;
        }

        if (0 == strncmp(pcmd, "exit", 4)) {
            break;
        }

        if (0 == strncmp(pcmd, "hit", 3)) {
            printf("cache hit rate:%.2f%%\n", evfs_query_cache_performance() * 100);
            continue;
        }

        if (0 == strncmp(pcmd, "ls", 2)) {
            evfs_test_iterate_all_entries();
            continue;
        }

        if (0 == strncmp(pcmd, "read", 4)) {
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_test_read_entry_data(handle, pcmd + 4);
            continue;
        }

        if (0 == strncmp(pcmd, "write", 5)) {
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_test_write_entry_data(handle, pcmd + 5);
            continue;
        }

        if (0 == strncmp(pcmd, "open", 4)) {
            if (handle > 0) {
                printf("order to close entry %d\n", handle);
                evfs_close_entry(handle);
            }
            handle = evfs_test_open_entry(pcmd + 4);
            continue;
        }

        if (0 == strncmp(pcmd, "new", 3)) {
            if (handle > 0) {
                printf("order to close entry %d\n", handle);
                evfs_close_entry(handle);
            }
            handle = evfs_test_new_entry(pcmd + 3);
            continue;
        }

        if (0 == strncmp(pcmd, "close", 5)) {
            if (handle > 0) {
                evfs_close_entry(handle);
                printf("close entry %d ok\n", handle);
                handle = -1;
            }
            continue;
        }

        if (0 == strncmp(pcmd, "flush", 5)) {
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_flush_entry_buffer(handle);
            continue;
        }

        if (0 == strncmp(pcmd, "import", 6)) {
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_test_import_file_to_entry(pcmd + 6, handle);
            continue;
        }

        if (0 == strncmp(pcmd, "export", 6)) {
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_test_export_entry_to_file(pcmd + 6, handle);
            continue;
        }

        if (0 == strncmp(pcmd, "delete", 6)) {
            evfs_test_delete_entry_by_name(pcmd + 6);
            continue;
        }

        showhelp();
    }

    if (is_opened) {
        evfs_close();
    }
    printf("bye\n");
    return 0;
}
