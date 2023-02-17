#include "args.h"

int main(int argc, char **argv)
{
    auto status = nsp::toolkit::singleton<args>::instance()->load_startup_parameters(argc, argv);
    if (!NSP_SUCCESS(status)) {
        return 1;
    }
    return 0;
}
