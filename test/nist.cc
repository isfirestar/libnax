#include <gtest/gtest.h>

#include "compiler.h"
#include "nis.h"
#include "ifos.h"

#include <unistd.h>
#include <stdio.h>

static void STDCALL TestTcpCallback(const struct nis_event *event, const void *data) {
    if (event->Event == EVT_RECEIVEDATA) {
        const tcp_data_t *tcp_data = (const tcp_data_t *)data;
        if (tcp_data->e.Packet.Size > 0)  {
            if (1 == tcp_data->e.Packet.Data[0]) {
                EXPECT_TRUE(0 == memcmp(tcp_data->e.Packet.Data + 1, "hello", 5));
                // wait one second for the synchoronous read preparing
                sleep(1);
                nsp_status_t status = tcp_write(event->Ln.Tcp.Link, "\2world", 6, NULL);
                EXPECT_GE(status, 0);
            }
        }
    }
}

static void STDCALL TestUdpCallback(const struct nis_event *event, const void *data) {
    if (event->Event == EVT_RECEIVEDATA) {
        const udp_data_t *udp = (const udp_data_t *)data;
        if (udp->e.Packet.Size > 0)  {
            if (1 == udp->e.Packet.Data[0]) {
                EXPECT_TRUE(0 == memcmp(udp->e.Packet.Data + 1, "hello", 5));
                // wait one second for the synchoronous read preparing
                sleep(1);
                nsp_status_t status = udp_write(event->Ln.Tcp.Link, "\2world", 6, udp->e.Packet.Domain, udp->e.Packet.RemotePort, NULL);
                EXPECT_GE(status, 0);
            }
        }
    }
}

TEST(DoTestTcpFlow, TestTcpFlow) {
    tcp_init2(0);
    // create server on explicit specify address and port
    HTCPLINK srv = tcp_create(TestTcpCallback, "127.0.0.1", 10222);
    EXPECT_NE(srv, INVALID_HTCPLINK);
    // create client on random port
    HTCPLINK cli = tcp_create(NULL, NULL, 0);
    EXPECT_NE(cli, INVALID_HTCPLINK);
    // set server listen
    nsp_status_t status = tcp_listen(srv, 100);
    EXPECT_TRUE(NSP_SUCCESS(status));
    // set client connected
    status = tcp_connect(cli, "127.0.0.1", 10222);
    EXPECT_TRUE(NSP_SUCCESS(status));
    // send some data to server from client
    status = tcp_write(cli, "\1hello", 6, NULL);
    EXPECT_GE(status, 0);
    // synchronous wait for server response
    char data[1024];
    status = tcp_read(cli, data, sizeof(data));
    EXPECT_GT(status, 0);
    EXPECT_TRUE(0 == memcmp(data, "\2world", 6));
    // ok, simple test complete, close all
    tcp_destroy(srv);
    tcp_destroy(cli);
    tcp_uninit();
}

TEST(DoTestTcpDomainFlow, TestTcpDomainFlow) {
    ifos_path_buffer_t file;
    ifos_getpedir(&file);
    char domain[300];
    sprintf(domain, "ipc:%s/%s", file.u.cst, "nist.sock");
    tcp_init2(0);
    // create server on explicit specify address and port
    HTCPLINK srv = tcp_create(TestTcpCallback, domain, 0);
    EXPECT_NE(srv, INVALID_HTCPLINK);
    // create client on random port
    HTCPLINK cli = tcp_create(NULL, "ipc:", 0);
    EXPECT_NE(cli, INVALID_HTCPLINK);
    // set server listen
    nsp_status_t status = tcp_listen(srv, 100);
    EXPECT_TRUE(NSP_SUCCESS(status));
    // set client connected
    status = tcp_connect(cli, domain, 0);
    EXPECT_TRUE(NSP_SUCCESS(status));
    // send some data to server from client
    status = tcp_write(cli, "\1hello", 6, NULL);
    EXPECT_GE(status, 0);
    // synchronous wait for server response
    char data[1024];
    status = tcp_read(cli, data, sizeof(data));
    EXPECT_GT(status, 0);
    EXPECT_TRUE(0 == memcmp(data, "\2world", 6));
    // ok, simple test complete, close all
    tcp_destroy(srv);
    tcp_destroy(cli);
    tcp_uninit();
}

TEST(DoTestUdpFlow, TestUdpFlow) {
    udp_init2(0);
    // create server on explicit specify address and port
    HUDPLINK srv = udp_create(TestUdpCallback, "127.0.0.1", 10222, UDP_FLAG_NONE);
    EXPECT_NE(srv, INVALID_HTCPLINK);
    // create client on random port
    HUDPLINK cli = udp_create(NULL, NULL, 0, UDP_FLAG_NONE);
    // send some data to server from client
    nsp_status_t status = udp_write(cli, "\1hello", 6, "127.0.0.1", 10222, NULL);
    EXPECT_GE(status, 0);
    // synchronous wait for server response
    char data[1024];
    nis_inet_addr addr;
    uint16_t port;
    status = udp_read(cli, data, sizeof(data), &addr, &port);
    EXPECT_GT(status, 0);
    EXPECT_TRUE(0 == memcmp(data, "\2world", 6));
    EXPECT_TRUE(0 == memcmp(addr.i_addr, "127.0.0.1", 9));
    // ok, simple test complete, close all
    udp_destroy(srv);
    udp_destroy(cli);
    udp_uninit();
}

TEST(DoTestUdpDomainFlow, TestUdpDomainFlow) {
    ifos_path_buffer_t file;
    ifos_getpedir(&file);
    char client_domain[300], server_domain[300];
    sprintf(client_domain, "ipc:%s/%s", file.u.cst, "udp_client_domain.sock");
    sprintf(server_domain, "ipc:%s/%s", file.u.cst, "udp_server_domain.sock");
    udp_init2(0);
    // create server on explicit specify address and port
    HUDPLINK srv = udp_create(TestUdpCallback, server_domain, 0, UDP_FLAG_NONE);
    EXPECT_NE(srv, INVALID_HTCPLINK);
    // create client on random port
    HUDPLINK cli = udp_create(NULL, client_domain, 0, UDP_FLAG_NONE);
    // send some data to server from client
    nsp_status_t status = udp_write(cli, "\1hello", 6, server_domain, 0, NULL);
    EXPECT_GE(status, 0);
    // synchronous wait for server response
    char data[1024];
    nis_inet_addr addr;
    status = udp_read(cli, data, sizeof(data), &addr, NULL);
    EXPECT_GT(status, 0);
    EXPECT_TRUE(0 == memcmp(data, "\2world", 6));
    // ok, simple test complete, close all
    udp_destroy(srv);
    udp_destroy(cli);
    udp_uninit();
}
