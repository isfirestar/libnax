#include "etc_reader.h"

#include "ifos.h"
#include "object.h"
#include "clist.h"
#include "avltree.h"

struct etcr_file;

struct etcr_kv_pair
{
    struct list_head entry_of_kvpair;
    struct avltree_node_t leaf_of_kvpair;
    union {
        char *key;
        const char *ckey;
    };
    union {
        char *value;
        const char *cvalue;
    };
};

struct etcr_line
{
    struct list_head entry_of_line;
    char *data;
};

struct etcr_file
{
    char *data;
    int64_t size;
    file_descriptor_t fd;
    struct list_head head_of_line;
    size_t count_of_line;
    struct list_head head_of_kvpair;
    struct avltree_node_t *root_of_kvpair;
    size_t count_of_kvpair;
};

static int _etcr_key_compare(const void *left, const void *right)
{
    struct etcr_kv_pair *lpair;
    struct etcr_kv_pair *rpair;
    lpair = container_of(left, struct etcr_kv_pair, leaf_of_kvpair);
    rpair = container_of(right, struct etcr_kv_pair, leaf_of_kvpair);

    return strcmp(lpair->key, rpair->key);
}

static char *_etcr_trim(char *data)
{
    char *head, *tail;

    head = data;
    while (*head == 0x20) {
        ++head;
    }

    tail = head + strlen(head) - 1;
    while (*tail == 0x20) {
        *tail = 0;
        --tail;
    }

    return head;
}

static char *_etcr_filter(char *line)
{
    char *cursor;

    cursor = line;
    while (*cursor) {
        if (*cursor == '#') {
            *cursor = 0;
            break;
        }
        cursor++;
    }

    if (*line == 0) {
        return NULL;
    }

    return _etcr_trim(line);
}

static nsp_status_t _etcr_parse_line(struct etcr_file *etcrf)
{
    struct etcr_line *line;
    nsp_status_t status;
    char cursor;
    size_t i;

    status = NSP_STATUS_SUCCESSFUL;
    line = NULL;
    for (i = 0; i < etcrf->size; i++) {
        if (!line) {
            line = (struct etcr_line *)ztrymalloc(sizeof(*line));
            if (!line) {
                status = posix__makeerror(ENOMEM);
                break;
            }
            line->data = &etcrf->data[i];
        } else {
            cursor = etcrf->data[i];
            if (cursor == '\n') {
                /* end this line */
                etcrf->data[i] = 0;
                /* filter comment line and trim effective data buffer */
                line->data = _etcr_filter(line->data);
                if (!line->data) {
                    zfree(line);
                } else {
                    list_add_tail(&line->entry_of_line, &etcrf->head_of_line);
                    etcrf->count_of_line++;
                }
                line = NULL;
            }

            /* compatible to windows line-endding */
            if (cursor == '\r') {
                etcrf->data[i] = 0;
            }
        }
    }

    if (!NSP_SUCCESS(status)) {
        while (NULL != (line = list_first_entry_or_null(&etcrf->head_of_line, struct etcr_line, entry_of_line))) {
            list_del(&line->entry_of_line);
            INIT_LIST_HEAD(&line->entry_of_line);
            zfree(line);
        }
    }

    return status;
}

static void _etcr_parse_kvpair(struct etcr_file *etcrf)
{
    struct list_head *n, *pos;
    struct etcr_line *line;
    char *chr;
    struct etcr_kv_pair *kvpair;

    list_for_each_safe(pos, n, &etcrf->head_of_line) {
        line = container_of(pos, struct etcr_line, entry_of_line);

        chr = strchr(line->data, '=');
        if (!chr) {
            list_del(&line->entry_of_line);
            INIT_LIST_HEAD(&line->entry_of_line);
            etcrf->count_of_line--;
            zfree(line);
        } else {
            kvpair = (struct etcr_kv_pair *)ztrymalloc(sizeof(*kvpair));
            if (likely(kvpair)) {
                *chr = 0;
                kvpair->key = _etcr_trim(line->data);
                kvpair->value = _etcr_trim(chr + 1);
                etcrf->root_of_kvpair = avlinsert(etcrf->root_of_kvpair, &kvpair->leaf_of_kvpair, &_etcr_key_compare);
                list_add_tail(&kvpair->entry_of_kvpair, &etcrf->head_of_kvpair);
                etcrf->count_of_kvpair++;
            }
        }
    }
}

static nsp_status_t _etcr_parse(struct etcr_file *etcrf)
{
    nsp_status_t status;

    status = _etcr_parse_line(etcrf);
    if (NSP_SUCCESS(status)) {
        _etcr_parse_kvpair(etcrf);
    }

    return status;
}

static int STDCALL _etcr_initializer(void *udata, const void *ctx, int ctxcb)
{
    struct etcr_file *etcrf;

    etcrf = (struct etcr_file *)udata;
    etcrf->fd = INVALID_FILE_DESCRIPTOR;
    etcrf->data = NULL;
    etcrf->size = 0;
    INIT_LIST_HEAD(&etcrf->head_of_line);
    etcrf->count_of_line = 0;
    etcrf->root_of_kvpair = NULL;
    INIT_LIST_HEAD(&etcrf->head_of_kvpair);
    etcrf->count_of_kvpair = 0;
    return 0;
}

static void STDCALL _etcr_unloader(objhld_t hld, void *udata)
{
    struct etcr_file *etcrf;
    struct etcr_line *line;
    struct etcr_kv_pair *kvpair;

    etcrf = (struct etcr_file *)udata;
    if (INVALID_FILE_DESCRIPTOR != etcrf->fd) {
        ifos_file_close(etcrf->fd);
        etcrf->fd = INVALID_FILE_DESCRIPTOR;
    }

    while (NULL != (line = list_first_entry_or_null(&etcrf->head_of_line, struct etcr_line, entry_of_line))) {
        list_del(&line->entry_of_line);
        INIT_LIST_HEAD(&line->entry_of_line);
        etcrf->count_of_line--;
        zfree(line);
    }

    while (NULL != (kvpair = list_first_entry_or_null(&etcrf->head_of_kvpair, struct etcr_kv_pair, entry_of_kvpair))) {
        list_del(&kvpair->entry_of_kvpair);
        INIT_LIST_HEAD(&kvpair->entry_of_kvpair);
        etcrf->root_of_kvpair = avlremove(etcrf->root_of_kvpair, &kvpair->leaf_of_kvpair, NULL, &_etcr_key_compare);
        etcrf->count_of_kvpair--;
        zfree(kvpair);
    }

    if (etcrf->data) {
        zfree(etcrf->data);
        etcrf->data = NULL;
    }
}

static const struct objcreator _etcr_creator = {
    .known = -1,
    .size = sizeof(struct etcr_file),
    .initializer = &_etcr_initializer,
    .unloader = &_etcr_unloader,
    .context = NULL,
    .ctxsize = 0,
};

PORTABLEIMPL(nsp_status_t) etcr_load_from_memory(const char *buffer, size_t size, objhld_t *out)
{
    nsp_status_t status;
    objhld_t hld;
    struct etcr_file *etcrf;

    if ( unlikely(!out || !buffer || 0 == size) ) {
        return posix__makeerror(EINVAL);
    }

    status = objallo4(&_etcr_creator, &hld);
    if (unlikely(!NSP_SUCCESS(status))) {
        return status;
    }
    etcrf = objrefr(hld);

    do {
        status = NSP_STATUS_FATAL;
        etcrf->size = size;
        etcrf->data = (char *)ztrymalloc((size_t)etcrf->size);
        if (!etcrf->data) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        memcpy(etcrf->data, buffer, size);
        status = _etcr_parse(etcrf);
    } while (0);

    if (!NSP_SUCCESS(status)) {
        objclos(hld);
    } else {
        *out = hld;
    }

    objdefr(hld);
    return status;
}

PORTABLEIMPL(nsp_status_t) etcr_load_from_harddisk(const etcr_path_t *file, objhld_t *out)
{
    nsp_status_t status;
    objhld_t hld;
    struct etcr_file *etcrf;
    int cb;

    if ( unlikely(!out || !file)) {
        return posix__makeerror(EINVAL);
    }

    status = objallo4(&_etcr_creator, &hld);
    if (unlikely(!NSP_SUCCESS(status))) {
        return status;
    }
    etcrf = objrefr(hld);

    do {
        status = ifos_file_open(file->cst, FF_RDACCESS | FF_OPEN_EXISTING, 0644, &etcrf->fd);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        etcrf->size = ifos_file_fgetsize(etcrf->fd);
        if (etcrf->size <= 0) {
            break;
        }

        etcrf->data = (char *)ztrymalloc((size_t)etcrf->size);
        if (!etcrf->data) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        cb = ifos_file_read(etcrf->fd, etcrf->data, (int)etcrf->size);
        if (cb != (int)etcrf->size) {
            break;
        }

        status = _etcr_parse(etcrf);
    } while (0);

    if (!NSP_SUCCESS(status)) {
        objclos(hld);
    } else {
        *out = hld;
    }

    objdefr(hld);
    return status;
}

PORTABLEIMPL(nsp_status_t) etcr_query_value_bykey(objhld_t hld, const char *key, const char ** const value)
{
    nsp_status_t status;
    struct etcr_file *etcrf;
    struct etcr_kv_pair *kvpair, find;
    struct avltree_node_t *found;

    if ( unlikely(hld <= 0 || !key) ) {
        return posix__makeerror(EINVAL);
    }

    etcrf = objrefr(hld);
    if (!etcrf) {
        return posix__makeerror(EINVAL);
    }

    find.ckey = key;
    found = avlsearch(etcrf->root_of_kvpair, &find.leaf_of_kvpair, &_etcr_key_compare);
    if (found) {
        status = NSP_STATUS_SUCCESSFUL;
        if (value) {
            kvpair = container_of(found, struct etcr_kv_pair, leaf_of_kvpair);
            *value = kvpair->value;
        }
    } else {
        status = posix__makeerror(ENOENT);
    }

    objdefr(hld);
    return status;
}

PORTABLEIMPL(void) etcr_unload(objhld_t hld)
{
    objclos(hld);
}

#if _TEST

#include <assert.h>

int main(int argc, char *argv[])
{
    nsp_status_t status;
    objhld_t hld;
    static const char test_buffer[] = "key1=value1\n"
            "key2=value2\n"
            " key3= value3\r\n"
            " key4 =value4 \n"
            "#key5 = value5\n"
            " # key6 = value6\n"
            "key7 $= value5\n"
            "key8 = value8  # these are some comment\n";
    const char *value;

    status = etcr_load_from_memory(test_buffer, strlen(test_buffer), &hld);
    assert(NSP_SUCCESS(status));
    status = etcr_query_value_bykey(hld, "key1", &value);
    assert( (NSP_SUCCESS(status) && 0 == strcmp(value, "value1")) );
    status = etcr_query_value_bykey(hld, "key2", &value);
    assert( (NSP_SUCCESS(status) && 0 == strcmp(value, "value2")) );
    status = etcr_query_value_bykey(hld, "key3", &value);
    assert( (NSP_SUCCESS(status) && 0 == strcmp(value, "value3")) );
    status = etcr_query_value_bykey(hld, "key4", &value);
    assert( (NSP_SUCCESS(status) && 0 == strcmp(value, "value4")) );
    status = etcr_query_value_bykey(hld, "key5", &value);
    assert( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key6", &value);
    assert( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key7", &value);
    assert( (NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ));
    status = etcr_query_value_bykey(hld, "key8", &value);
    assert( (NSP_SUCCESS(status) && 0 == strcmp(value, "value8")) );

    etcr_unload(hld);
    return 0;
}
#endif
