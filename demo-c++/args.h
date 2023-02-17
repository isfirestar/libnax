#pragma once

#include "endpoint.h"
#include "singleton.hpp"

#include "nis.h"

#include <string>

// type
enum class demo_sess_type : int8_t
{
    SESS_TYPE_UNKNOWN = -1,
    SESS_TYPE_SERVER = 0,
    SESS_TYPE_CLIENT = 1,
};

// mode
enum class demo_cs_mode : int8_t
{
    CS_MODE_ERROR			= 0,
    CS_MODE_ESCAPE_TASK     = 1,     // 普通数据交互运行
    CS_MODE_MUST_BE_CLIENT  = CS_MODE_ESCAPE_TASK,
    CS_MODE_FILE_UPLOAD     = 2,    // 文件上传模式
    CS_MODE_FILEMODE		= CS_MODE_FILE_UPLOAD,
    CS_MODE_FILE_DOWNLOAD   = 3,
};

class args
{
    struct legal_argument
    {
        // @0: server @1:client
        demo_sess_type type = demo_sess_type::SESS_TYPE_CLIENT;     
        // IP地址或域名         
        std::string host = "0.0.0.0";                
        // 端口   
        uint16_t port = 10256;          
        // 包大小
        uint32_t size = MAX_TCP_UNIT - 8/*nsp head*/ - 24/*proto_head*/ - 4/*proto_string_t*/;      
        // 统计信息打印间隔   
        uint32_t interval = 1000;      
        // 使用文件
        std::string file;     
        // 任务模式    
        demo_cs_mode mode = demo_cs_mode::CS_MODE_ESCAPE_TASK;
        // 交换窗口大小
        int winsize = 1;		    
    }  argument_;
    friend class nsp::toolkit::singleton<args>;

private:
    args() = default;
    void display_usage();
    void display_author_information();

public:
    ~args() = default;
    nsp_status_t load_startup_parameters(int argc, char **argv);

public:
    nsp_status_t buildep(nsp::tcpip::endpoint &ep) const;
    demo_sess_type gettype() const; // 获取身份类型
    int getpkgsize() const; // 获取单帧大小
    int getinterval() const; // 获取间隔
    demo_cs_mode getmode() const;  // 获取运行行为方式
    void getfile(std::string &file) const; // 获取IO文件
    int getwinsize() const; // 获取窗口大小
};
