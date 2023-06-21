#include "rax.h"

#include <stdio.h>

// gcc rax.c entry.c zmalloc.c -g3 -orax.exe -lm
int main(void) {
    // create a new tree
    rax *myrax = raxNew();

    // insert key-value pairs
    raxInsert(myrax, (unsigned char*)"foo", 3, (void*)1, NULL);
    raxInsert(myrax, (unsigned char*)"foobar", 6, (void*)2, NULL);
    raxInsert(myrax, (unsigned char*)"footer", 6, (void*)3, NULL);
    raxInsert(myrax, (unsigned char*)"first", 5, (void*)4, NULL);
    raxInsert(myrax, (unsigned char*)"bar", 3, (void*)5, NULL);
    raxInsert(myrax, (unsigned char*)"foobar", 6, (void*)6, NULL);
    raxInsert(myrax, (unsigned char*)"foo", 3, (void*)7, NULL);  // cover the case 1
    raxInsert(myrax, (unsigned char*)"foot", 4, (void*)8, NULL);
    raxInsert(myrax, (unsigned char*)"football", 8, (void*)9, NULL);
    raxInsert(myrax, (unsigned char*)"foobaz", 6, (void*)10, NULL);
    raxInsert(myrax, (unsigned char*)"bar", 3, (void*)11, NULL);
    raxInsert(myrax, (unsigned char*)"bar", 3, (void*)12, NULL);
    raxInsert(myrax, (unsigned char*)"bart", 4, (void*)13, NULL);
    raxInsert(myrax, (unsigned char*)"bartender", 9, (void*)14, NULL);
    raxInsert(myrax, (unsigned char*)"zoo", 3, (void*)15, NULL);
    raxInsert(myrax, (unsigned char*)"zoology", 7, (void*)16, NULL);
    raxInsert(myrax, (unsigned char*)"zoologist", 9, (void*)17, NULL);
    raxInsert(myrax, (unsigned char*)"zoolo", 5, (void*)18, NULL);
    raxInsert(myrax, (unsigned char*)"foo", 3, (void*)19, NULL);    // cover the case 7
    raxInsert(myrax, (unsigned char*)"foo", 3, (void*)20, NULL);  // cover the case 19

    // traverse the tree
    raxIterator iter;
    raxStart(&iter, myrax);

    // wildcard of seek op codes are: ^, $, <, >, =
    raxSeek(&iter, "^", NULL, 0);
    while (raxNext(&iter)) {
        printf("key: %.*s, data: %p\n", (int)iter.key_len, iter.key, iter.data);
    }

    // search "foo" and print it's value
    void *data = raxFind(myrax, (unsigned char*)"foo", 3);
    printf("foo: %p\n", data);

    return 0;
}

