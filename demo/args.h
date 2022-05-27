#if !defined NSHOST_ECHO_ARGS_H
#define NSHOST_ECHO_ARGS_H

#include "compiler.h"

struct argument {
    int type;
    char chost[255];
    char shost[255];
    uint16_t port;
    int echo;
    int mute;
    int length;
    int mode;
};

extern nsp_status_t arg_check_startup(int argc, char **argv);
extern const struct argument *arg_get_parameter();

#endif
