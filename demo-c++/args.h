#pragma once

#include "endpoint.h"
#include "nis.h"

#include <string>

// type
enum class demo_sess_type : int8_t
{
    SESS_TYPE_UNKNOWN = -1,
    SESS_TYPE_SERVER = 's',
    SESS_TYPE_CLIENT = 'c',
};

// mode
enum class demo_interact_mode : int8_t
{
    CS_MODE_NORMAL = 0,
    CS_MODE_MUTE = 1,
    CS_MODE_ECHO = 2,
};

enum class demo_trans_proto : int8_t
{
    DT_IPPROTO_UNKNOWN = 0,
    DT_IPPROTO_TCP = 1,
    DT_IPPROTO_UDP = 2,
};

class args
{
    struct legal_argument
    {
        // @0: server @1:client
        demo_sess_type type = demo_sess_type::SESS_TYPE_SERVER;     
        // IP��ַ������         
        std::string host = "0.0.0.0";           
        std::string chost = "0.0.0.0";     
        // �˿�   
        uint16_t port = 10256;    
        // ����ģʽ    
        demo_interact_mode mode = demo_interact_mode::CS_MODE_NORMAL;
        // �������ݴ�С
        int length = 64;		    
        // ѡ��ʹ��Э��
        demo_trans_proto proto = demo_trans_proto::DT_IPPROTO_TCP;
    } argument_;

private:
    void display_usage();
    void display_author_information();

public:
    args() = default;
    ~args() = default;
    nsp_status_t load_startup_parameters(int argc, char **argv);

public:
    nsp_status_t get_specify_host(nsp::tcpip::endpoint &ep) const;
    demo_sess_type get_sess_type() const; // ��ȡ�������
    int get_package_size() const; // ��ȡ��֡��С
    int get_display_interval() const; // ��ȡ���
    demo_interact_mode get_interact_mode() const;  // ��ȡ������Ϊ��ʽ
    demo_trans_proto get_trans_proto() const; // ��ȡ����Э��
    void display_statistic(double delta);
    void get_server_host(std::string &host) const;
    void get_client_host(std::string &chost) const;
};
