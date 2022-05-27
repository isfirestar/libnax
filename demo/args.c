#include "args.h"

#include <getopt.h>

#include "abuff.h"

static struct argument  __startup_parameters;

enum ope_index {
    kOptIndex_GetHelp = 'h',
    kOptIndex_GetVersion = 'v',

    kOptIndex_SetPort = 'p',
    kOptIndex_Server = 's',
    kOptIndex_Client = 'c',

    kOptIndex_EchoModel = 'e',
    kOptIndex_MuteServer = 'm',
    kOptIndex_DataLength = 'l',

    kOptIndex_UdpMode = 'u',
    kOptIndex_TcpMode = 't',
};

static const struct option long_options[] = {
    {"help", no_argument, NULL, kOptIndex_GetHelp},
    {"version", no_argument, NULL, kOptIndex_GetVersion},
    {"port", required_argument, NULL, kOptIndex_SetPort},
    {"echo", no_argument, NULL, kOptIndex_EchoModel},
    {"server", optional_argument, NULL, kOptIndex_Server},
    {"mute", no_argument, NULL, kOptIndex_MuteServer},
    {"client", optional_argument, NULL, kOptIndex_Client},
    {"data-length", required_argument, NULL, kOptIndex_DataLength},
    {"udp", no_argument, NULL, kOptIndex_UdpMode},
    {"tcp", no_argument, NULL, kOptIndex_TcpMode},
    {NULL, 0, NULL, 0}
};

static void arg_display_usage()
{
    static const char *usage_context =
            "usage: nstest {-v|--version|-h|--help}\n"
            "[-p | --port [port]]\tto specify the connect target in client or change the local listen port in server\n"
            "[-e | --echo]\trun program in echo model\n"
            "[-m | --mute]\tonly effective in server model\n"
            "\t\twhen this argument has been specified, server are in silent model, nothing response to client\n"
            "\t\totherwise in default, all packages will completely consistent response to client\n"
            "[-s | --server [[opt]linstener]]\tset this argument order run a server program on specified IP address or IPC file\n"
            "\t\tfor example: \" -s 220.135.241.47 \" order program create a listen TCP socket on specified IP address\n"
            "\t\t\" -s ipc:/dev/shm/server.sock \" order program create a IPC mode TCP link associated with file system target\n"
            "[-c | --client [[opt]local]]\tset this argument indicate run a client program.[local] can be both local IP address or IPC file\n"
            "\t\tfor example: \" -c 192.168.0.9 \" order program create a client TCP socket on specified IP address\n"
            "\t\t\" -c ipc:/dev/shm/client.sock -u \" order program create a IPC mode UDP link associated with file system target\n"
            "\t\t\" -c ipc: -s ipc:/dev/shm/server.sock \" order program create a IPC mode TCP link which connect to file which specify in -s region\n"
            "[-l | --data-length [bytes]]\tset the size of each packets to send(client) or to response(server)\n"
            "[-u | --udp]\tuse UDP mode\n"
            "[-t | --tcp]\tuse TCP mode, this is default mode\n"
            ;

    printf("%s", usage_context);
}

#define VERSION_STRING "VersionOfProgramDefinition-ChangeInMakefile"
static void arg_display_author_information()
{
    static char author_context[512];
    sprintf(author_context, "nstest\n%s\n"
            "Copyright (C) 2017 Neo.Anderson\n"
            "For bug reporting instructions, please see:\n"
            "<http://www.nsplibrary.com.cn/>.\n"
            "For help, type \"help\".\n", VERSION_STRING);
    printf("%s", author_context);
}

nsp_status_t arg_check_startup(int argc, char **argv)
{
    int opt_index;
    int opt;
    char shortopts[128];

    memset(&__startup_parameters, 0, sizeof(__startup_parameters));
    __startup_parameters.type = 's';
    crt_strcpy(__startup_parameters.shost, sizeof(__startup_parameters.shost), "0.0.0.0");
    crt_strcpy(__startup_parameters.chost, sizeof(__startup_parameters.chost), "0.0.0.0");
    __startup_parameters.port = 10256;
    __startup_parameters.echo = 0;
    __startup_parameters.mute = 0;
    __startup_parameters.length = 1024;
    __startup_parameters.mode = 't';

    /* double '::' meat option may have argument or not,
        one ':' meat option MUST have argument,
        no ':' meat option MUST NOT have argument */
    crt_strcpy(shortopts, sizeof(shortopts), "hvp:es::mc::tl:u");
    opt = getopt_long(argc, argv, shortopts, long_options, &opt_index);
    while (opt != -1) {
        switch (opt) {
            case 'h':
                arg_display_usage();
                return NSP_STATUS_FATAL;
            case 'v':
                arg_display_author_information();
                return NSP_STATUS_FATAL;
            case 'p':
                assert(optarg);
                __startup_parameters.port = (uint16_t) strtoul(optarg, NULL, 10);
                break;
            case 's':
                if (optarg) {
                    crt_strcpy(__startup_parameters.shost, sizeof(__startup_parameters.shost), optarg);
                }
                break;
            case 'e':
                 __startup_parameters.echo = opt;
                break;
            case 'u':
            case 't':
                __startup_parameters.mode = opt;
                break;
            case 'm':
                __startup_parameters.mute = opt;
                break;
            case 'c':
                if (optarg) {
                    crt_strcpy(__startup_parameters.chost, sizeof(__startup_parameters.chost), optarg);
                }
                __startup_parameters.type = 'c';
                break;
            case 'l':
                assert(optarg);
                __startup_parameters.length = atoi(optarg);
                break;
            case '?':
                printf("?\n");
            case 0:
                printf("0\n");
            default:
                arg_display_usage();
                return NSP_STATUS_FATAL;
        }
        opt = getopt_long(argc, argv, shortopts, long_options, &opt_index);
    }

    return NSP_STATUS_SUCCESSFUL;
}

const struct argument *arg_get_parameter()
{
    return &__startup_parameters;
}
