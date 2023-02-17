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
    CS_MODE_ESCAPE_TASK     = 1,     // ��ͨ���ݽ�������
    CS_MODE_MUST_BE_CLIENT  = CS_MODE_ESCAPE_TASK,
    CS_MODE_FILE_UPLOAD     = 2,    // �ļ��ϴ�ģʽ
    CS_MODE_FILEMODE		= CS_MODE_FILE_UPLOAD,
    CS_MODE_FILE_DOWNLOAD   = 3,
};

class args
{
    struct legal_argument
    {
        // @0: server @1:client
        demo_sess_type type = demo_sess_type::SESS_TYPE_CLIENT;     
        // IP��ַ������         
        std::string host = "0.0.0.0";                
        // �˿�   
        uint16_t port = 10256;          
        // ����С
        uint32_t size = MAX_TCP_UNIT - 8/*nsp head*/ - 24/*proto_head*/ - 4/*proto_string_t*/;      
        // ͳ����Ϣ��ӡ���   
        uint32_t interval = 1000;      
        // ʹ���ļ�
        std::string file;     
        // ����ģʽ    
        demo_cs_mode mode = demo_cs_mode::CS_MODE_ESCAPE_TASK;
        // �������ڴ�С
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
    demo_sess_type gettype() const; // ��ȡ�������
    int getpkgsize() const; // ��ȡ��֡��С
    int getinterval() const; // ��ȡ���
    demo_cs_mode getmode() const;  // ��ȡ������Ϊ��ʽ
    void getfile(std::string &file) const; // ��ȡIO�ļ�
    int getwinsize() const; // ��ȡ���ڴ�С
};
