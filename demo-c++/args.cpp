#include "args.h"

#include <getopt.h>
#include <iostream>

#include "endpoint.h"

enum ope_index {
    kOptIndex_GetHelp = 'h',
    kOptIndex_GetVersion = 'v',
    kOptIndex_SetHost = 'H', // @client: 链接目标IP地址获域名 @server:本地监听的IP地址/网卡
    kOptIndex_SetPort = 'P', // @client: 连接目标的端口 @server:本地监听的端口
    kOptIndex_Server = 'S', // 以服务端身份运行
    kOptIndex_Client = 'C', // 以客户端身份运行
    kOptIndex_Size = 's', // @client: 请求包大小 @server:应答包大小, 如果指定了 @u或者@d, 则该参数表示文件传输的单片大小
    kOptIndex_DisplayInterval = 'i', // 打印间隔，秒单位
    kOptIndex_UploadFile = 'u', // 执行一次上传文件操作
    kOptIndex_DownloadFile = 'd', // 执行一次下载文件操作
	kOptIndex_WinSize = 'n',	// winsize
	kOptIndex_WindowSize = 'w',  // winsize
};

static const struct option long_options[] = {
    {"help", no_argument, NULL, kOptIndex_GetHelp},
    {"host", required_argument, NULL, kOptIndex_SetHost},
    {"version", no_argument, NULL, kOptIndex_GetVersion},
    {"port", required_argument, NULL, kOptIndex_SetPort},
    {"server", no_argument, NULL, kOptIndex_Server},
    {"client", no_argument, NULL, kOptIndex_Client},
    {"size", required_argument, NULL, kOptIndex_Size},
    {"interval", required_argument, NULL, kOptIndex_DisplayInterval},
    {"upload", required_argument, NULL, kOptIndex_UploadFile },
    {"download", required_argument, NULL, kOptIndex_DownloadFile},
	{"winsize", required_argument, NULL, kOptIndex_WinSize},
	{"window-size", required_argument, NULL, kOptIndex_WindowSize},
    {NULL, 0, NULL, 0}
};

void args::display_usage()
{
    static const char *usage_context =
            "usage escape - nshost escape timing counter\n"
            "SYNOPSIS\n"
            "\t[-h|--help]\tdisplay usage context and help informations\n"
            "\t[-v|--version]\tdisplay versions of executable archive\n"
            "\t[-S|--server]\tthis session run as a server.\n"
            "\t[-C|--client]\tthis session run as a client.this option is default.\n"
            "\t[-H|--host]\ttarget server IPv4 address or domain when @C or local bind adpater when @S, \"0.0.0.0\" by default\n"
            "\t[-P|--port]\ttarget server TCP port when @C or local listen TCP port when @S, 10256 by default\n"
            "\t[-s|--size]\trequest packet size in bytes when @C or response packet size in bytes when @S 1448 by default\n"
			"\t\t\twhen using @u or @d, @s means pre-block bytes in file mode\n"
            "\t[-i|--interval]\tinterval in seconds between two statistic point.1 second by default. \n"
            "\t[-u|--upload]\tfile mode of upload from @args on local to server application startup directory. only when @C.\n"
            "\t[-d|--download]\tfile mode of download from @args on server to current directory.only @C.\n"
			"\t[-n|-w|--winsize|--window-size]\tpacket transfer window size of this connection. 1 by default. only @C and conflict with file mode.\n"
            ;
    std::cout << usage_context;
}

void args::display_author_information()
{
    static const char *author_context =
            "nshost escape 1,1,0,0\n"
            "Copyright (C) 2017 neo.anderson\n"
            "For bug reporting instructions, please see:\n"
            "<http://www.nshost.com.cn/>.\n"
            "For help, type \"help\".\n"
            ;
    std::cout << author_context;
}

nsp_status_t args::load_startup_parameters(int argc, char **argv)
{
    static const std::string shortopts = "SCvhH:P:s:i:u:d:n:w:";
    int idx;
    int opt = ::getopt_long(argc, argv, shortopts.c_str(), long_options, &idx);
    while (opt != -1) {
        switch (opt) {
            case 'S':
                this->argument_.type = demo_sess_type::SESS_TYPE_SERVER;
                break;
            case 'C':
                this->argument_.type = demo_sess_type::SESS_TYPE_CLIENT;
                break;
            case 'v':
                display_author_information();
                return NSP_STATUS_FATAL;
            case 'h':
                display_usage();
                return NSP_STATUS_FATAL;
            case 'H':
                this->argument_.host.assign(optarg);
                break;
            case 'P':
                this->argument_.port = (uint16_t) strtoul(optarg, NULL, 10);
                break;
            case 's':
                this->argument_.size = strtoul(optarg, NULL, 10);
                break;
            case 'i':
                this->argument_.interval = strtoul(optarg, NULL, 10) * 1000;
                break;
            case 'u':
                this->argument_.mode = demo_cs_mode::CS_MODE_FILE_UPLOAD;
                this->argument_.file.assign(optarg);
                break;
            case 'd':
                this->argument_.mode = demo_cs_mode::CS_MODE_FILE_DOWNLOAD;
                this->argument_.file.assign(optarg);
                break;
			case 'w':
			case 'n':
				this->argument_.winsize = strtoul(optarg, NULL, 10);
				break;
            case '?':
                printf("?\n");
            case 0:
                printf("0\n");
            default:
                display_usage();
                return NSP_STATUS_FATAL;
        }
        opt = getopt_long(argc, argv, shortopts.c_str(), long_options, &idx);
    }

    if ((this->argument_.mode > demo_cs_mode::CS_MODE_MUST_BE_CLIENT) && this->argument_.type != demo_sess_type::SESS_TYPE_CLIENT) {
        this->display_usage();
        return NSP_STATUS_FATAL;
    }
	if ( this->argument_.type == demo_sess_type::SESS_TYPE_CLIENT && this->argument_.host == "0.0.0.0") {
		this->display_usage();
		return NSP_STATUS_FATAL;
	}
	if ( this->argument_.winsize > 1 && this->argument_.type == demo_sess_type::SESS_TYPE_SERVER ) {
		this->display_usage();
		return NSP_STATUS_FATAL;
	}
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t args::buildep(nsp::tcpip::endpoint &ep) const
{
    return nsp::tcpip::endpoint::build(this->argument_.host.c_str(), this->argument_.port, ep);
}

demo_sess_type args::gettype() const
{
    return this->argument_.type;
}

int args::getpkgsize() const
{
    return this->argument_.size;
}

int args::getinterval() const
{
    return this->argument_.interval;
}

demo_cs_mode args::getmode() const
{
    return this->argument_.mode;
}

void args::getfile(std::string &file) const
{
    file = this->argument_.file;
}

int args::getwinsize() const
{
	return this->argument_.winsize;
}
