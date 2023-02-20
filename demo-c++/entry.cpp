#include "dispatch.h"

int main(int argc, char **argv)
{
    auto status = nsp::toolkit::singleton<dispatcher>::instance()->start(argc, argv);
    if (!NSP_SUCCESS(status)) {
        return 1;
    }
    return 0;
}
