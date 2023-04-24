#include "demo.h"

#include "threading.h"
#include "ifos.h"
#include "zmalloc.h"

static void on_tcp_received(HTCPLINK link, const unsigned char *data, int size)
{
    const struct argument *parameter;
    char *resp;
    pid_t tid;

    parameter = arg_get_parameter();

    tid = (pid_t)nis_cntl(link, NI_GETRXTID);
    assert(tid == ifos_gettid());

    resp = NULL;
    nis_cntl(link, NI_GETCTX, &resp);

    do {
        if (parameter->mute) {
            return;
        }

        if (parameter->echo) {
            display(link, data, size);
            /* roll back */
            if ( tcp_write(link, data, size, NULL) < 0 ) {
                break;
            }
            return;
        }

        if (parameter->length > 0 && resp) {
            if ( tcp_write(link, resp, parameter->length, NULL) < 0 ) {
                break;
            }
        }

        return;
    } while (0);

    tcp_destroy(link);
}

static void on_tcp_accepted(HTCPLINK local, HTCPLINK accepted)
{
    char *resp;
    const struct argument *parameter;

    parameter = arg_get_parameter();

    if (parameter->length > 0 && !parameter->mute) {
        resp = zmalloc(parameter->length);
        nis_cntl(accepted, NI_SETCTX, resp);
    }
}

static void STDCALL tcp_server_callback(const struct nis_event *event, const void *data)
{
    struct nis_tcp_data *tcpdata;

    tcpdata = (struct nis_tcp_data *)data;
    switch(event->Event) {
        case EVT_RECEIVEDATA:
            on_tcp_received(event->Ln.Tcp.Link, tcpdata->e.Packet.Data, tcpdata->e.Packet.Size);
            break;
        case EVT_TCP_ACCEPTED:
            on_tcp_accepted(event->Ln.Tcp.Link, tcpdata->e.Accept.AcceptLink);
            break;
        case EVT_PRE_CLOSE:
            if (tcpdata->e.PreClose.Context) {
                zfree((struct demo_context *)tcpdata->e.PreClose.Context);
            }
            break;
        case EVT_CLOSED:
        default:
            break;
    }
}

static void on_udp_received(HUDPLINK link, const unsigned char *data, int size, const char *rhost, uint16_t rport)
{
    const struct argument *parameter;
    char *resp;

    parameter = arg_get_parameter();

    resp = NULL;
    nis_cntl(link, NI_GETCTX, &resp);

    do {
        if (parameter->mute) {
            return;
        }

        if (parameter->echo) {
            display(link, data, size);
            /* roll back */
            if ( udp_write(link, data, size, rhost, rport, NULL) < 0 ) {
                break;
            }
            return;
        }

        if (parameter->length > 0 && resp) {
            if ( udp_write(link, resp, parameter->length, rhost, rport, NULL) < 0 ) {
                break;
            }
        }

        return;
    } while (0);

    udp_destroy(link);
}

static void on_udp_domain_received(HUDPLINK link, const unsigned char *data, int size, const char *rpath)
{
    const struct argument *parameter;
    char *resp;
    char fpath[108];

    parameter = arg_get_parameter();

    resp = NULL;
    nis_cntl(link, NI_GETCTX, &resp);

    do {
        if (parameter->mute) {
            return;
        }

        if (rpath) {
            snprintf(fpath, sizeof(fpath) - 1,  "IPC:%s", rpath);
        }

        if (parameter->echo) {
            display(link, data, size);
            /* roll back */
            if (rpath) {
                if ( udp_write(link, data, size, fpath, 0, NULL) < 0 ) {
                    break;
                }
            }

            return;
        }

        if (parameter->length > 0 && resp) {
            if (rpath) {
                if ( udp_write(link, resp, parameter->length, fpath, 0, NULL) < 0 ) {
                    break;
                }
            }
        }

        return;
    } while (0);

    udp_destroy(link);
}

static void STDCALL udp_server_callback(const struct nis_event *event, const void *data)
{
    struct nis_udp_data *udpdata;

    udpdata = (struct nis_udp_data *)data;

    switch(event->Event) {
        case EVT_RECEIVEDATA:
            on_udp_received(event->Ln.Udp.Link, udpdata->e.Packet.Data, udpdata->e.Packet.Size,
                udpdata->e.Packet.RemoteAddress, udpdata->e.Packet.RemotePort);
            break;
        case EVT_UDP_RECEIVE_DOMAIN:
            on_udp_domain_received(event->Ln.Udp.Link, udpdata->e.Packet.Data, udpdata->e.Packet.Size,
                udpdata->e.Packet.Domain);
            break;
        case EVT_PRE_CLOSE:
            if (udpdata->e.PreClose.Context) {
                zfree(udpdata->e.PreClose.Context);
            }
            break;
        case EVT_CLOSED:
        default:
            break;
    }
}

static nsp_status_t start_tcp_server(const struct argument *parameter, lwp_event_t *exit)
{
    HTCPLINK server;
    int attr;

    /* ipc:/dev/shm/demo.sock */
    server = tcp_create2(&tcp_server_callback, parameter->shost, parameter->port, gettst());
    if (INVALID_HTCPLINK == server) {
        return NSP_STATUS_FATAL;
    }

    /* allow accept update tst */
    attr = nis_cntl(server, NI_GETATTR);
    if (attr >= 0 ) {
        attr |= LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT;
        attr = nis_cntl(server, NI_SETATTR, attr);
    }

    /* begin listen */
    tcp_listen(server, 100);
    /* block the program auto exit */
    lwp_event_wait(exit, -1);
    tcp_destroy(server);
    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t start_udp_server(const struct argument *parameter, lwp_event_t *exit)
{
    HUDPLINK server;
    char *resp;

    resp = NULL;
    if (parameter->length > 0 && !parameter->mute) {
        resp = zmalloc(parameter->length);
    }

    /* ipc:/dev/shm/demo.sock */
    server = udp_create(&udp_server_callback, parameter->shost, parameter->port, 0);
    if (INVALID_HTCPLINK == server) {
        return NSP_STATUS_FATAL;
    }

    /* nail the response data pointer to link */
    if (resp) {
        nis_cntl(server, NI_SETCTX, resp);
    }

    /* block the program auto exit */
    lwp_event_wait(exit, -1);
    udp_destroy(server);
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t start_server(const struct argument *parameter, lwp_event_t *exit)
{
    return (('u' == parameter->mode) ? start_udp_server(parameter, exit) : start_tcp_server(parameter, exit));
}
