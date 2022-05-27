#include "demo.h"

#include "naos.h"
#include "logger.h"
#include "print.h"
#include "ifos.h"
#include "abuff.h"

#include <signal.h>     /* signal(2) */

static const unsigned char NSPDEF_OPCODE[4] = { 'N', 's', 'p', 'd' };

#pragma pack(push, 1)

typedef struct {
    uint32_t op_;
    uint32_t cb_;
} nsp__tst_head_t;

#pragma pack(pop)

nsp_status_t STDCALL nsp__tst_parser(void *dat, int cb, int *pkt_cb)
{
	nsp__tst_head_t *head = (nsp__tst_head_t *)dat;

	if (!head) {
		return posix__makeerror(EINVAL);
	}

	if (0 != memcmp(NSPDEF_OPCODE, &head->op_, sizeof(NSPDEF_OPCODE))) {
		return NSP_STATUS_FATAL;
	}

	*pkt_cb = head->cb_;
	return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t STDCALL nsp__tst_builder(void *dat, int cb)
{
	nsp__tst_head_t *head = (nsp__tst_head_t *)dat;

	if (!dat || cb <= 0) {
		return posix__makeerror(EINVAL);
	}

	memcpy(&head->op_, NSPDEF_OPCODE, sizeof(NSPDEF_OPCODE));
	head->cb_ = cb;
	return NSP_STATUS_SUCCESSFUL;
}

const tst_t *gettst()
{
	/* nail the TST and then create TCP link */
	static const tst_t tst = {
		.parser_ = &nsp__tst_parser,
		.builder_ = &nsp__tst_builder,
		.cb_ = sizeof(nsp__tst_head_t)
	};
	return &tst;
}

void display(HTCPLINK link, const unsigned char *data, int size)
{
	char output[1024];
	int offset;

	offset = crt_sprintf(output, sizeof(output) - 1, "income : %s", data);
	ifos_file_write(1, output, offset);
}

void STDCALL nshost_ecr(const char *host_event, const char *reserved, int rescb)
{
	if (host_event) {
		print("%s", host_event);
	}
}

typedef nsp_status_t (*init_t)(int);
typedef void (*uninit_t)();
static lwp_event_t __exit;
static void master_sig_handler(int signum)
{
    if (SIGINT == signum) {
        lwp_event_awaken(&__exit);
    }
}

int main(int argc, char **argv)
{
	nsp_status_t status;
	const struct argument *parameter;
	init_t proto_init;
	uninit_t proto_uninit;

	status = arg_check_startup(argc, argv);
	if (!NSP_SUCCESS(status)) {
		return 1;
	}

	log_init();
	nis_checr(&nshost_ecr);
	lwp_event_init(&__exit, LWPEC_SYNC);

	parameter = arg_get_parameter();
	if (parameter->mode == 'u') {
        proto_init = &udp_init2;
        proto_uninit = &udp_uninit;
    } else {
        proto_init = &tcp_init2;
        proto_uninit = &tcp_uninit;
    }

    proto_init(4);
	if (parameter->type == 's') {
		signal(SIGINT, &master_sig_handler);
		start_server(parameter, &__exit);
	}

	if (parameter->type == 'c') {
		if (0 == parameter->echo) {
			signal(SIGINT, &master_sig_handler);
		}
		start_client(parameter, &__exit);
	}
	proto_uninit();
	return 0;
}
