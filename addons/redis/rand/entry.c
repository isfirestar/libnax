#include "rand.h"

#include <stdio.h>

int main() {
    redisSrand48(1);
    for (int i = 0; i < 10; ++i) {
        printf("%d\n", redisLrand48());
    }
    return 0;
}
