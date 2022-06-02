#include "args.h"

int main(int argc, char **argv)
{
    nsp_status_t status = nsp::toolkit::singleton<argument>::instance()->check_startup(argc, argv);
    if (!NSP_SUCCESS(status)) {
        return 1;
    }
    return 0;
}
