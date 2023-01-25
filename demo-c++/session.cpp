#include "session.h"
#include "os_util.hpp"
#include "toolkit.h"
#include "args.h"

#include <stdio.h>
#include <string>

static const char *getname(const char *path)
{
    const char *symb = strrchr(path, '/');
    if (!symb) {
        symb = strrchr(path, '\\');
    }
    return symb;
}

///////////////////////////////////////////////////// session //////////////////////////////////////////////////////

session::session() : base_session(), client_init_finish(0), client_established_notify_(0)
{
    ;
}

session::session(HTCPLINK lnk) : base_session(lnk), client_init_finish(0)
{
    ;
}

session::~session()
{
    if (file_task) {
        delete file_task;
    }
    if (escape_task) {
        delete escape_task;
    }
}

// 响应客户端登陆请求
int session::on_login(const std::basic_string<unsigned char> &data)
{
    struct proto_login login;
    int cb = data.size();
    if (login.build((const unsigned char *) data.c_str(), cb) < 0) {
        return -1;
    }

    // 记录本链接的工作模式
    this->mode = login.mode;

    proto_login_ack login_ack;
    login_ack.err = 0;

    // 常规 escape task 的请求
    if (CS_MODE_ESCAPE_TASK == mode) {
        try {
            // 构建 escape task 结构
            if (!escape_task) {
                escape_task = new struct proto_escape_task;
                escape_task->head.type = PKTTYPE_ESCAPE_TASK_ACK;

                int size = getpkgsize();
                char *tmpstr = new char[size];
                escape_task->contex.assign(tmpstr, size);
                delete[]tmpstr;
            }
        } catch (...) {
            login_ack.err = ENOMEM;
        }
    }

    // CS_MODE_FILE_UPLOAD 或 CS_MODE_FILE_DOWNLOAD 不需要做任何额外处理
    // 等待 enable_filemode 的请求到达
    else {
        nspinfo << "client file mode login.";
    }

    return this->psend(&login_ack);
}

int session::on_login_client(const std::basic_string<unsigned char> &data)
{
    proto_login_ack login_ack;
    int cb = data.size();
    if (login_ack.build((const unsigned char *) data.c_str(), cb) < 0) {
        return -1;
    }

    switch (this->mode) {
            // escape task 类型的连接，建立对象， 并开始工作
        case CS_MODE_ESCAPE_TASK:
            {
                try {
                    escape_task = new struct proto_escape_task;
                    int size = getpkgsize();
                    char *tmpstr = new char[size];
                    escape_task->contex.assign(tmpstr, size);
                    delete[]tmpstr;
                } catch (...) {
                    return -1;
                }

				// 触发 winsize 个escape task
				for ( int i = 0; i < getwinsize(); i++ ) {
					send_escape_next();
				}
				return 0;
            }
            break;

            // 如果请求上传文件， 则应该申请文件在本地和服务端的权限
        case CS_MODE_FILE_UPLOAD:
            {
                // 建立上传请求前， 应该初始化本地文件权限
                try {
                    file_task = new file_mode(getfile(0), getpkgsize());
                    if (0 != file_task->open_it()) {
                        delete file_task;
                        file_task = nullptr;
                        client_inited = -1;
                        client_init_finish.sig();
                        return -1;
                    }
                } catch (...) {
                    return -1;
                }
                struct proto_enable_filemode filemode_request;
                int len;
                const char *file = getfile(&len);
                // 上传操作，不需要携带路径
                const char *symb = getname(file);
                std::string tmpstr;
                if (symb) {
                    tmpstr.assign(symb + 1, strlen(symb) - 1);
                }else{
                    tmpstr.assign(file);
                }
                filemode_request.path = tmpstr;
                filemode_request.block_size = getpkgsize();
                filemode_request.mode = ENABLE_FILEMODE_TEST; // 先尝试进行上传文件请求， 需要服务端处理文件已经存在等异常
                return psend(&filemode_request);
            }
            break;

            // 下载文件，不需要考虑服务端的文件权限， 仅需要考虑本地额的文件权限
            // 但是无论本地权限如何， 均需要发送 enable filemode 请求
        case CS_MODE_FILE_DOWNLOAD:
            {
                struct proto_enable_filemode filemode_request;
            }
            break;

        default:
            return -1;
    }

    return 0;
}

// 服务端处理 escape task
int session::on_escape_task(const std::basic_string<unsigned char> &data)
{
    struct proto_escape_task escape_request;
    int cb = data.size();
    if (escape_request.build((const unsigned char *) data.c_str(), cb) < 0) {
        return -1;
    }

    // 没有登陆 或 工作模式不符合 直接促成断开
    if (!this->escape_task || this->mode != CS_MODE_ESCAPE_TASK) {
        return -1;
    }

    escape_task->head.id = escape_request.head.id;
    escape_task->head.tx_timestamp = escape_request.head.tx_timestamp;
	return send_escape_next();
}

// 客户端处理 escape task
int session::on_escape_task_client(const std::basic_string<unsigned char> &data)
{
    struct proto_escape_task escape_response;
    int cb = data.size();
    if (escape_response.build((const unsigned char *) data.c_str(), cb) < 0) {
        return -1;
    }

    // 通知初始化完成
    if (client_inited < 0) {
        client_inited = 0;
        client_init_finish.sig();
    }

    my_stat.update_rtt(escape_response.head.tx_timestamp.value_);

    escape_task->head.tx_timestamp = nsp::os::clock_monotonic();
	return send_escape_next();
}

// 服务端处理文件请求
int session::on_enable_filemode(const std::basic_string<unsigned char> &data)
{
    struct proto_enable_filemode filemode_enable_request;
    int cb = data.size();
    if (filemode_enable_request.build((const unsigned char *) data.c_str(), cb) < 0) {
        return -1;
    }

    // 工作模式必须是文件模式
    if (mode < CS_MODE_FILEMODE) {
        return -1;
    }

    // 上传文件请求, 根据客户端请求类型，建立文件权限， 或者建立覆盖询问
    if (mode == CS_MODE_FILE_UPLOAD) {
        struct proto_enable_filemode_ack filemode_enable_response;
        try {
            std::string file_path(nsp::os::get_module_directory<char>());
            file_path += POSIX__DIR_SYMBOL;
            file_path += filemode_enable_request.path;
            file_task = new file_mode(file_path, getpkgsize());

            if (filemode_enable_request.mode == ENABLE_FILEMODE_FORCE) {
                filemode_enable_response.err = file_task->cover_it();
            } else {
                filemode_enable_response.err = file_task->creat_it();
            }

            // 无论发生什么错误， 即便是 EEXIST， 也关闭本次的描述符
            // EEXIST 情况，需要客户端确认是否覆盖该文件
            if (0 != filemode_enable_response.err) {
                delete file_task;
                file_task = nullptr;
            }
        } catch (...) {
            filemode_enable_response.err = ENOMEM;
        }
        return psend(&filemode_enable_response);
    }

    return -1;
}

int session::on_enable_filemode_client(const std::basic_string<unsigned char> &data)
{
    struct proto_enable_filemode_ack filemode_enable_ack;
    int cb = data.size();
    if (filemode_enable_ack.build((const unsigned char *) data.data(), cb) < 0) {
        return -1;
    }

    // 工作模式必须符合要求
    if (mode < CS_MODE_FILEMODE) {
        return -1;
    }

    // 上传模式
    if (mode == CS_MODE_FILE_UPLOAD) {

        // 上传模式走到这一步， 本地文件权限肯定已经打开
        if (!file_task) {
            return -1;
        }

        // 如果服务端没有该路径的权限， 则需要询问是否覆盖该文件
        if (filemode_enable_ack.err == EEXIST) {
            printf("file is existed,do you want to cover it?(yes/no):");
            char select_command[128];
            fgets(select_command, sizeof (select_command), stdin);
            if ('y' == select_command[0] || 'Y' == select_command[0]) {
                struct proto_enable_filemode filemode_request;
                int len;
                const char *path = getfile(&len);
                const char *symb = getname(path);
                std::string tmpstr;
                if (symb) {
                    tmpstr.assign(symb + 1, strlen(symb) - 1);
                }else{
                    tmpstr.assign(path);
                }
                filemode_request.path = tmpstr;
                filemode_request.block_size = getpkgsize();
                filemode_request.mode = ENABLE_FILEMODE_FORCE; // 先尝试进行上传文件请求， 需要服务端处理文件已经存在等异常
                return psend(&filemode_request);
            }
            client_inited = -1;
            client_init_finish.sig();
            return -1;
        }

        // 正常， 上端没有发生任何问题， 文件已经建立, 触发第一个上传数据片
        else if (0 == filemode_enable_ack.err) {
            client_inited = send_upload_next();
            client_init_finish.sig();
            return 0;
        }

        // 发生其他异常， 服务器无法处理， 直接促成断连
        else {
            client_inited = -1;
            client_init_finish.sig();
            return -1;
        }
    }

    return 0;
}

// 服务端处理上传数据
int session::on_file_block(const std::basic_string<unsigned char> &data)
{
    struct proto_file_block file_block;
    int cb = data.size();
    if (file_block.build((const unsigned char *) data.data(), cb) < 0) {
        return -1;
    }

    // 文件权限和工作模式必须吻合
    if (!file_task || this->mode < CS_MODE_FILEMODE) {
        return -1;
    }

    if (mode == CS_MODE_FILE_UPLOAD) {
        // 传输完成， 断开链接后直接可以释放文件
        if (0 == file_block.data.size() || OFFSET_EOF == file_block.offset) {
            return -1;
        }

        // 发生失败都以端链作为处理方案
        int wcb = (int)file_block.data.size();
        if (wcb != file_task->write_block((const unsigned char *)file_block.data.data(), file_block.offset, wcb)) {
            return -1;
        }

        struct proto_file_block_ack file_block_ack;
        return psend(&file_block_ack);
    }
    return 0;
}

int session::on_file_block_client(const std::basic_string<unsigned char> &data)
{
    struct proto_file_block_ack file_block;
    int cb = data.size();
    if (file_block.build((const unsigned char *) data.data(), cb) < 0) {
        return -1;
    }

    if (!file_task || this->mode < CS_MODE_FILEMODE ) {
        return -1;
    }

    // 继续尝试发送下一片文件
    if (this->mode == CS_MODE_FILE_UPLOAD) {
        return send_upload_next();
    }

    return 0;
}

void session::on_recvdata(const std::basic_string<unsigned char> &data)
{
    int retval;
    struct proto_head head;
    int cb = data.size();
    if (head.build((const unsigned char *) data.c_str(), cb) < 0) {
        this->close();
        return;
    }

    retval = -1;
    switch (head.type.value_) {
        case PKTTYPE_LOGIN:
            retval = on_login(data);
            break;
        case PKTTYPE_LOGIN_ACK:
            retval = on_login_client(data);
            break;
        case PKTTYPE_ENABLE_FILEMODE:
            retval = on_enable_filemode(data);
            break;
        case PKTTYPE_ENABLE_FILEMODE_ACK:
            retval = on_enable_filemode_client(data);
            break;
        case PKTTYPE_FILE_BLOCK:
            retval = on_file_block(data);
            break;
        case PKTTYPE_FILE_BLOCK_ACK:
            retval = on_file_block_client(data);
            break;
        case PKTTYPE_ESCAPE_TASK:
            retval = on_escape_task(data);
            break;
        case PKTTYPE_ESCAPE_TASK_ACK:
            retval = on_escape_task_client(data);
            break;
        default:
            break;
    }

    if (retval < 0) {
        this->close();
    } else {
		my_stat.increase_rx( data.size() );
    }
}

extern void end_client();

void session::on_disconnected(const HTCPLINK previous)
{
    // 客户端链接断开， 直接找主线程， 通知退出
    if (SESS_TYPE_CLIENT == type) {
        end_client();
    }
}

int session::connect_timeout(uint32_t timeo)
{
	if (0 != client_established_notify_.wait(-1)){
		this->close();
		return -1;
	}

	client_established_notify_.reset();
	return 0;
}

void session::on_connected()
{
	// 作为客户端成功连接上服务器
	client_established_notify_.sig();
}

void session::print()
{
	my_stat.print();
}

// 启动客户端， 初始化客户端的基本配置信息， 并登入服务器
int session::begin_client()
{
    this->mode = getmode();
    this->type = gettype();
    struct proto_login login_request;
    login_request.mode = this->mode;
    return this->psend(&login_request);
}

int session::waitfor_init_finish()
{
    if (0 != client_init_finish.wait(-1)) {
        return -1;
    }

    return client_inited;
}

// 发送下一片上传数据
int session::send_upload_next()
{
    struct proto_file_block file_block_upload;
    try {
        unsigned char *block = new unsigned char[file_task->get_blocksize()];
        int rdcb = file_task->read_block(block);
        if (rdcb <= 0) {
            file_block_upload.offset = OFFSET_EOF;
        } else {
            file_block_upload.offset = file_task->get_previous_offset();
            file_block_upload.data.assign(block, rdcb);
        }
        delete[]block;
    } catch (...) {
        return -1;
    }

	my_stat.increase_tx( file_block_upload.length() );
    return psend(&file_block_upload);
}

int session::send_escape_next()
{
    // 加入统计
	my_stat.increase_tx( escape_task->length() );
    return psend(escape_task);
}
