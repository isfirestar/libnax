// create a test for redis dict

#include <stdio.h>
#include <string.h>

#include "dict.h"
#include "zmalloc.h"

// the treverse function for dict
void print_dict(dict *d) {
    dictIterator *iter = dictGetIterator(d);
    dictEntry *entry = NULL;
    while ((entry = dictNext(iter)) != NULL) {
        printf("key: %s, val: %s\n", (char *)entry->key, (char *)entry->v.val);
    }
}

uint64_t hashCallback(const void *key) {
    return dictGenHashFunction((unsigned char*)key, strlen((char*)key));
}

int compareCallback(void *privdata, const void *key1, const void *key2) {
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = strlen((char*)key1);
    l2 = strlen((char*)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void freeCallback(void *privdata, void *val) {
    ;
}

dictType BenchmarkDictType = {
    hashCallback,
    NULL,
    NULL,
    compareCallback,
    freeCallback,
    NULL,
    NULL
};

int main(int argc, char **argv) {
    // step 1, create a dict and add 5 entries to it
    dict *d = dictCreate(&BenchmarkDictType, NULL);
    dictAdd(d, "Julie", "24");
    dictAdd(d, "Tom", "23");
    dictAdd(d, "Lisa", "21");
    dictAdd(d, "Neo", "22");
    dictAdd(d, "Luise", "25");

    // step 2, treverse the dict and print all element of it
    print_dict(d);

    // step 3. try to add a entry which the key already exists, and than, search the key and print the value
    dictAdd(d, "Julie", "26");
    printf("after add a entry which the key already exists\n");
    dictEntry *entry = dictFind(d, "Julie");
    printf("key: %s, val: %s\n", (char *)entry->key, (char *)entry->v.val);

    // step 4. remove one entry from the dict, and than, treverse the dict and print all element of it
    dictDelete(d, "Julie");
    printf("after remove one entry from the dict\n");
    print_dict(d);

    // step 5. try to remove a entry which it's key does not exists, watch the result
    int retv = dictDelete(d, "Julie");
    printf("after remove a entry which it's key does not exists, retv: %d\n", retv);
    return 0;
}
