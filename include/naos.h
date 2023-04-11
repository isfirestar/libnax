#if !defined POSIX_NAOS_H
#define POSIX_NAOS_H

/*
 * naos.h Define some OS-independent functions
 * anderson 2017-05-08
 */

#include "compiler.h"
#include "abuff.h"

typedef abuff_type(16) abuff_naos_inet_t;

/* Switching IPv4 representation method between Dotted-Decimal-Notation and integer
 */
PORTABLEAPI(uint32_t) naos_ipv4tou(const char *inetstr, enum byte_order_t byte_order);
PORTABLEAPI(nsp_status_t) naos_ipv4tos(uint32_t inet, abuff_naos_inet_t *inetstr);

/* the same as htonl(3)/ntohl(3)/ntohs(3)/htons(3)
 */
PORTABLEAPI(uint32_t) naos_chord32(uint32_t value);
PORTABLEAPI(uint16_t) naos_chord16(uint16_t value);

/* verfiy the IP address string */
PORTABLEAPI(nsp_boolean_t) naos_is_legal_ipv4(const char *inetstr);

/* generic a hexadecimal text dump for specify buffer with it's fix length
 * @naos_hexdump didn't deal with buffer which have length large than 65535 bytes
 * @columns parameter indicate the output align columns, 8 by default and restrict in range [2,32]
 * if @on_dump callback function is a nullptr, dump text will write to stdout directly
 * the upper restrict of @length is 8192 byte */
PORTABLEAPI(void) naos_hexdump(const unsigned char *buffer, uint16_t length, uint8_t columns, void (*on_dump)(const char *text, uint32_t length));

/* parse a command line like string @cmdline to @target
 * every command line is split by space
 * words which start with '"' will be treated as a whole string
 * output parameter @target MUST allocate by caller, @max parameter indicate the max element count of @target
 * on success, return the element count of @target, otherwise return -1
 * warning: after this method called, source buffer which pointer to by @cmdline are already been destroied(the splitor space are overridden by \0) */
PORTABLEAPI(int) naos_cmdline_like_analyze(char *cmdline, char **target, int max);

#endif
