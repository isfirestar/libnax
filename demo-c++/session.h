#if !defined SESSION_H
#define SESSION_H

#include <atomic>
#include <map>

#include "application_network_framwork.hpp"
#include "old.hpp"
#include "serialize.hpp"

#include "filemode.h"
#include "sess_stat.h"
#include "proto.hpp"
#include "args.h"

#include "os_util.hpp"

typedef nsp::tcpip::tcp_application_client<nsp::proto::nspdef::protocol> base_session;

class session : public base_session {
    sess_stat my_stat;
    file_mode *file_task = nullptr;
    struct proto_escape_task *escape_task = nullptr;
    std::atomic<uint32_t> ato_request_id{ 0};
    int type = SESS_TYPE_UNKNOWN;
    int mode = CS_MODE_ERROR;
    nsp::os::waitable_handle client_init_finish;
    std::atomic<int> client_inited{ -1};
	nsp::os::waitable_handle client_established_notify_;

private:
    int on_login(const std::basic_string<unsigned char> &data);
    int on_login_client(const std::basic_string<unsigned char> &data);
    int on_escape_task(const std::basic_string<unsigned char> &data);
    int on_escape_task_client(const std::basic_string<unsigned char> &data);
    int on_enable_filemode(const std::basic_string<unsigned char> &data);
    int on_enable_filemode_client(const std::basic_string<unsigned char> &data);
    int on_file_block(const std::basic_string<unsigned char> &data);
    int on_file_block_client(const std::basic_string<unsigned char> &data);

private:
    int send_upload_next();
    int send_escape_next();

public:
    session();
    session(HTCPLINK lnk);
    virtual ~session();

    virtual void on_recvdata(const std::basic_string<unsigned char> &data) override;
    virtual void on_disconnected(const HTCPLINK previous) override;
	virtual void on_connected() override;

	int connect_timeout(uint32_t timeo);
    int begin_client();
    int waitfor_init_finish();

    void print();
};

#endif