#include "session.h"

#include "dispatch.h"

///////////////////////////////////////////////////// session //////////////////////////////////////////////////////

demo_session::demo_session() : tcp_session_client()
{
    ;
}

demo_session::demo_session(HTCPLINK lnk) : tcp_session_client(lnk)
{
    ;
}

demo_session::~demo_session()
{
}

void demo_session::on_recvdata(const std::basic_string<unsigned char> &data)
{
    nsp::toolkit::singleton<dispatcher>::instance()->on_tcp_recvdata(data);
}

void demo_session::on_disconnected(const HTCPLINK previous)
{
    nsp::toolkit::singleton<dispatcher>::instance()->on_disconnected(previous);
}

void demo_session::on_connected()
{
}
