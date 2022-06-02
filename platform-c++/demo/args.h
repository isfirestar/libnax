#if !defined NSHOST_ECHO_ARGS_H
#define NSHOST_ECHO_ARGS_H

#include "compiler.h"

#include <getopt.h>

#include <string>

#include "singleton.hpp"

class argument
{
    const std::string VERSION_STRING;
    friend class nsp::toolkit::singleton<argument>;

private:
    std::string author_context;

    argument();
    void display_usage();
    void display_author_information();
public:
    ~argument();
    nsp_status_t check_startup(int argc, char **argv);

    int type;
    char chost[255];
    char shost[255];
    uint16_t port;
    int echo;
    int mute;
    int length;
    int mode;
};

#endif
