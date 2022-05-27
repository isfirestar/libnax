#include "demo.h"

#include "ifos.h"
#include "zmalloc.h"
#include "atom.h"

static uint64_t __bytes_tx = 0;
static uint64_t __bytes_rx = 0;

static void initial_echo_client(HTCPLINK link, const struct argument *parameter)
{
    char *p;
    char text[65536];
    size_t n;
    nsp_status_t status;

    ifos_file_write(STDOUT_FILENO, "input:$ ", 8);

    while ( NULL != (p = fgets(text, sizeof(text), stdin)) ) {
        n = strlen(text);
        if ( n == 0) {
            continue;
        }

        if (0 == strncasecmp("exit", text, 4)) {
            break;
        }

        if (parameter->mode == 'u') {
            status = udp_write(link, text, n, parameter->shost, parameter->port, 0);
        } else {
            status = tcp_write(link, text, n, NULL);
        }

        if (!NSP_SUCCESS(status)) {
            break;
        }
    }
}

static void on_tcp_received(HTCPLINK link, const unsigned char *data, unsigned short size)
{
    const struct argument *parameter;
    char *resp;

    parameter = arg_get_parameter();

    resp = NULL;
    nis_cntl(link, NI_GETCTX, &resp);

    if (parameter->echo) {
        display(link, data, size);
        ifos_file_write(STDOUT_FILENO, "input:$ ", 8);
    } else {
        atom_increase(&__bytes_rx, size);
        if (resp && parameter->length > 0) {
            if ( tcp_write(link, resp, parameter->length, NULL) >= 0 ) {
                atom_increase(&__bytes_tx, parameter->length);
            }
        }
    }
}

static void STDCALL tcp_client_callback(const struct nis_event *event, const void *data)
{
    struct nis_tcp_data *tcpdata = (struct nis_tcp_data *)data;

    switch(event->Event) {
        case EVT_RECEIVEDATA:
            on_tcp_received(event->Ln.Tcp.Link, tcpdata->e.Packet.Data, tcpdata->e.Packet.Size);
            break;
        case EVT_PRE_CLOSE:
            assert(tcpdata->e.PreClose.Context);
            zfree(tcpdata->e.PreClose.Context);
            break;
        case EVT_CLOSED:
        default:
            break;
    }
}

static void on_udp_received(HUDPLINK link, const unsigned char *data, unsigned short size)
{
    const struct argument *parameter;
    char *resp;

    parameter = arg_get_parameter();

    resp = NULL;
    nis_cntl(link, NI_GETCTX, &resp);

    if (parameter->echo) {
        display(link, data, size);
        ifos_file_write(STDOUT_FILENO, "input:$ ", 8);
    } else {
        atom_increase(&__bytes_rx, size);
        if (resp && parameter->length > 0) {
            if ( udp_write(link, resp, parameter->length, parameter->shost, parameter->port, 0) >= 0 ) {
                atom_increase(&__bytes_tx, parameter->length);
            }
        }
    }
}

static void STDCALL udp_client_callback(const struct nis_event *event, const void *data)
{
    struct nis_udp_data *udpdata = (struct nis_udp_data *)data;

    switch(event->Event) {
        case EVT_RECEIVEDATA:
        case EVT_UDP_RECEIVE_DOMAIN:
            on_udp_received(event->Ln.Udp.Link, udpdata->e.Packet.Data, udpdata->e.Packet.Size);
            break;

            //on_udp_domain_received(event->Ln.Udp.Link, udpdata->e.Domain.Data, udpdata->e.Domain.Size);
            break;
        case EVT_PRE_CLOSE:
            assert(udpdata->e.PreClose.Context);
            zfree(udpdata->e.PreClose.Context);
            break;
        case EVT_CLOSED:
        default:
            break;
    }
}

static nsp_status_t start_tcp_client(const struct argument *parameter, HLNK *out)
{
    HTCPLINK link;
    char *resp;
    nsp_status_t status;

    status = NSP_STATUS_FATAL;
    do {
        resp = NULL;
        if (parameter->length > 0 && !parameter->mute) {
            resp = zmalloc(parameter->length);
        }

        /* ipc:/dev/shm/demo.sock */
        link = tcp_create2(&tcp_client_callback, parameter->chost, 0, gettst());
        if (INVALID_HTCPLINK == link) {
            break;
        }
        nis_cntl(link, NI_SETCTX, resp);
        //nis_cntl(client, NI_SETATTR, LINKATTR_TCP_NO_BUILD | LINKATTR_TCP_FULLY_RECEIVE);

        status = tcp_connect(link, parameter->shost, parameter->port);
        if ( !NSP_SUCCESS(status)) {
            break;
        }

        if (out) {
            *out = link;
        }
        return NSP_STATUS_SUCCESSFUL;
    } while(0);

    if (INVALID_HTCPLINK != link) {
        tcp_destroy(link);
    } else {
        if(resp) {
            zfree(resp);
        }
    }
    return status;
}

static nsp_status_t start_udp_client(const struct argument *parameter, HLNK *out)
{
    HUDPLINK link;
    char *resp;

    resp = NULL;
    if (parameter->length > 0 && !parameter->mute) {
        resp = zmalloc(parameter->length);
    }

    /* ipc:/dev/shm/demo.sock */
    link = udp_create(&udp_client_callback, parameter->chost, parameter->port, 0);
    if (INVALID_HUDPLINK == link) {
        if (resp) {
            free(resp);
        }
        return NSP_STATUS_FATAL;
    }

    /* nail the response data pointer to link */
    nis_cntl(link, NI_SETCTX, resp);
    if (out) {
        *out = link;
    }
    return NSP_STATUS_SUCCESSFUL;
}

static size_t format_delta(double delta, char *display)
{
    size_t n;
    int i;
    static const char *unit[4] = { "B/s", "KB/s", "MB/s", "GB/s" };

    n = 0;

    for (i = 0; i < 4; i++) {
        if (delta < 1024) {
            n = sprintf(display, "%.2f %s", delta, unit[i]);
            break;
        }
        delta /= 1024;
    }

    if (4 == i) {
        n = sprintf(display, "%.2f %s", delta, unit[3]);
    }

    return n;
}

static void trace_performance(lwp_event_t *exit)
{
    size_t i;
    char display[1024];
    uint64_t pre_tx, pre_rx, now_tx, now_rx;
    double delta_tx, delta_rx;
    nsp_status_t status;

    i = sprintf(display, "Tx\t\t\tRx\n");
    ifos_file_write(STDOUT_FILENO, display, i);
    pre_tx = atom_get(&__bytes_tx);
    pre_rx = atom_get(&__bytes_rx);
    while( posix__makeerror(ETIMEDOUT) == (status = lwp_event_wait(exit, 1000)) ) {
        i = 0;
        now_tx = atom_get(&__bytes_tx);
        now_rx = atom_get(&__bytes_rx);
        delta_tx = now_tx - pre_tx;
        delta_rx = now_rx - pre_rx;

        i += format_delta(delta_tx, display + i);
        i += sprintf(display + i, "\t\t");
        i += format_delta(delta_rx, display + i);
        i += sprintf(display + i, "\n");
        ifos_file_write(STDOUT_FILENO, display, i);

        pre_tx = now_tx;
        pre_rx = now_rx;
    }
}

nsp_status_t start_client(const struct argument *parameter, lwp_event_t *exit)
{
    HLNK link;
    int mode;
    nsp_status_t status;
    char *p;

    mode = parameter->mode;
    status = ('u' == mode) ? start_udp_client(parameter, &link) : start_tcp_client(parameter, &link);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    /* into echo model */
    do {
        if ('e' == parameter->echo) {
            initial_echo_client(link, parameter);
            break;
        }

        /* statistic model */
        p = zmalloc(parameter->length);
        if ('u' == parameter->mode) {
            status = udp_write(link, p, parameter->length, parameter->shost, parameter->port, 0);
        } else {
            status = tcp_write(link, p, parameter->length, NULL);
        }
        if (!NSP_SUCCESS(status)) {
            zfree(p);
            return status;
        }
        atom_increase(&__bytes_tx, parameter->length);
        zfree(p);
        trace_performance(exit);
    } while (0);

    if ('u' == parameter->mode) {
        udp_destroy(link);
    } else {
        tcp_destroy(link);
    }
    return status;
}
