#pragma once

#include "application_network_framework.hpp"
#include "old.hpp"

typedef nsp::tcpip::tcp_application_client<nsp::proto::nspdef::protocol> tcp_session_client;

class demo_session : public tcp_session_client 
{
public:
    demo_session();
    demo_session(HTCPLINK lnk);
    virtual ~demo_session();

    virtual void on_recvdata(const std::basic_string<unsigned char> &data) override;
    virtual void on_disconnected(const HTCPLINK previous) override;
	virtual void on_connected() override;
};

typedef nsp::tcpip::tcp_application_service<tcp_session_client> tcp_session_server;
