#include "args.h"

#include "endpoint.h"

#include <getopt.h>
#include <iostream>

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

void args::display_usage()
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
    std::cout << usage_context;
}

void args::display_author_information()
{
    static char author_context[512];

    (void)snprintf(author_context, sizeof(author_context) - 1, "libnax-demo\n%s\n"
            "Copyright (C) 2017 Neo.Anderson\n"
            "For bug reporting instructions, please see:\n"
            "<http://www.nsplibrary.com.cn/>.\n"
            "For help, type \"help\".\n", _BUILTIN_VERSION);
    std::cout << author_context;
}

nsp_status_t args::load_startup_parameters(int argc, char **argv)
{
    static const std::string shortopts = "hvp:es::mc::tl:u";
    int opt_index;
    int opt = getopt_long(argc, argv, shortopts.c_str(), long_options, &opt_index);
    while (opt != -1) {
        switch (opt) {
            case 'h':
                display_usage();
                return NSP_STATUS_FATAL;
            case 'v':
                display_author_information();
                return NSP_STATUS_FATAL;
            case 'p':
                assert(optarg);
                argument_.port = (uint16_t) strtoul(optarg, NULL, 10);
                break;
            case 's':
                if (optarg) {
                    argument_.host = optarg;
                }
                break;
            case 'e':
                argument_.mode = demo_interact_mode::CS_MODE_ECHO;
                break;
            case 'm':
                argument_.mode = demo_interact_mode::CS_MODE_MUTE;
                break;
            case 'u':
                argument_.proto = demo_trans_proto::DT_IPPROTO_UDP;
                break;
            case 't':
                argument_.proto = demo_trans_proto::DT_IPPROTO_TCP;
                break;
            case 'c':
                if (optarg) {
                    argument_.chost = optarg;
                }
                argument_.type = demo_sess_type::SESS_TYPE_CLIENT;
                break;
            case 'l':
                assert(optarg);
                argument_.length = atoi(optarg);
                break;
            case '?':
                printf("?\n");
            case 0:
                printf("0\n");
            default:
                display_usage();
                return NSP_STATUS_FATAL;
        }
        opt = getopt_long(argc, argv, shortopts.c_str(), long_options, &opt_index);
    }

	if ( this->argument_.type == demo_sess_type::SESS_TYPE_CLIENT && this->argument_.host == "0.0.0.0") {
		this->display_usage();
		return NSP_STATUS_FATAL;
	}
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t args::get_specify_host(nsp::tcpip::endpoint &ep) const
{
    return nsp::tcpip::endpoint::build(this->argument_.host.c_str(), this->argument_.port, ep);
}

void args::get_server_host(std::string &host) const
{
    host = argument_.host;
}

void args::get_client_host(std::string &chost) const
{
    chost = argument_.chost;
}

demo_sess_type args::get_sess_type() const
{
    return this->argument_.type;
}

int args::get_package_size() const
{
    return this->argument_.length;
}

demo_interact_mode args::get_interact_mode() const
{
    return this->argument_.mode;
}

demo_trans_proto args::get_trans_proto() const
{
    return this->argument_.proto;
}

void args::display_statistic(double delta)
{
    static const std::string unit[4] = { "B/s", "KB/s", "MB/s", "GB/s" };

    for (int i = 0; i < 4; i++) {
        if (delta < 1024) {
            std::cout << delta << " " << unit[i];
            return;
        }
        delta /= 1024;
    }
    std::cout << delta << " " << unit[3];
}
