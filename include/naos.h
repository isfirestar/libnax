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

#endif
