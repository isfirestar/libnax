#include "evfs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

#include "ifos.h"

char *strtrim(char *str)
{
    char *end;

    while (isspace(*str)) {
        str++;
    }

    if (*str == 0) {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) {
        end--;
    }

    *(end + 1) = 0;

    return str;
}

// split string by specify symbol
// @str: input string
// @symbol: specify symbol
// @target: output array, each element point to a string
// @max: max element count of @target
// @target allocate by this function, caller has responsibility to free it
int strsplit(char *str, char symbol, char **target, int max)
{
    int count = 0;
    char *cursor = strtrim(str);

    while (cursor && count < max) {
        if (0 == *cursor) {
            break;
        }
        target[count] = cursor;
        count++;
        cursor = strchr(cursor, symbol);
        if (cursor) {
            *cursor = 0;
            cursor++;
        }
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
    int count, off, len, maxlen, i;
    nsp_status_t status;
    char *buffer;

    count = strsplit(pcmd, 0x20, target, 2);
    if (count < 1) {
        printf("usage : evfs read [offset][length] \n");
        return;
    }

    off = atoi(target[0]);
    if (count >= 2) {
        len = atoi(target[1]);
    } else {
        len = 0;
    }

    maxlen = evfs_query_entry_length(handle);
    if (maxlen <= 0) {
        printf("illegal size? %d\n", maxlen);
        return;
    }

    status = evfs_seek_entry_offset(handle, off);
    if (!NSP_SUCCESS(status)) {
        printf("failed seek entry offset to %d\n", off);
        return;
    }   

    buffer = (char *)malloc(maxlen + 1);
    if (!buffer) {
        return;
    }
    memset(buffer, 0, maxlen + 1);
    i = evfs_read_entry(handle, buffer, min(maxlen,len));
    if (i > 0 ) {
        printf("there are %d bytes reading from entry\n", i);
        printf("%s\n", buffer);
    }
    free(buffer);
 }

 void evfs_test_write_entry_data(evfs_entry_handle_t handle, char *pcmd)
 {
    char symbol = 0x20, *target[2];
    int off, count;
    nsp_status_t status;
    size_t bufferlen;
    char *buffer;
    
    count = strsplit(pcmd, symbol, target, 2);
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

    bufferlen = strlen(target[0]) + 1;
    buffer = (char *)malloc(bufferlen);
    if (!buffer) {
        return;
    }
    strcpy(buffer, target[0]);
    count = evfs_write_entry(handle, buffer, bufferlen);
    printf("there are %d bytes written to entry\n",count);
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
    char symbol = 0x20, *target[1];
    char *filename;
    file_descriptor_t fd;
    nsp_status_t status;
    int64_t fsize;
    static const int max_file_size = 10 << 20;
    char *buffer;

    count = strsplit(pcmd, symbol, target,1);
    if (count < 1) {
        printf("usage : evfs import [file]\n");
        return;
    }

    filename = target[0];

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
    char symbol = 0x20, *target[1];
    char *filename;
    file_descriptor_t fd;
    nsp_status_t status;
    int64_t fsize;
    static const int max_file_size = 10 << 20;
    char *buffer;

    count = strsplit(pcmd, symbol, target,1);
    if (count < 1) {
        printf("usage : evfs export [file]\n");
        return;
    }

    filename = target[0];

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
            if (handle < 0) {
                printf("you must open or create a entry frist.\n");
                continue;
            }
            evfs_earse_entry(handle);
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
