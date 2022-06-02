#if !defined ETC_READER_H
#define ETC_READER_H

/* this addons build for simple text config file read
 * moudle guarantee the life-cycle of object but didn't permissvie the multi-thread security
 * we don't provide write ability in this module
 * current addon version : 1.0.1
 * history version :
 *      1.0.1 author neo.anderson 2022/06/02
 */

#include "compiler.h"
#include "abuff.h"
#include "object.h"

typedef abuff_type(255) etcr_path_t;

/* @etcr_load_from_memory or @etcr_load_from_harddisk use to load config data from memory or harddisk file.
 * on success, return value shall be NSP_STATUS_SUCCESSFUL and @out indicate the effective target object.
 *  otherwise, negative error number returned */
PORTABLEAPI(nsp_status_t) etcr_load_from_memory(const char *buffer, size_t size, objhld_t *out);
PORTABLEAPI(nsp_status_t) etcr_load_from_harddisk(const etcr_path_t *file, objhld_t *out);

/* @etcr_query_value_bykey use to query the value of specify key @key, return to by pointer @value when successful
 * calling thread able to pass NULL to parameter @value, this behavior meat test @key exist or not.
 * @hld is the handle of config target object which allocated by either @etcr_load_from_memory or @etcr_load_from_harddisk
 * notes that : output value pointer @value is unmodifiability, because the memory address is owned by framework itself */
PORTABLEAPI(nsp_status_t) etcr_query_value_bykey(objhld_t hld, const char *key, const char ** const value);

/* every target object shall call @etcr_unload after successful invoke load functions */
PORTABLEAPI(void) etcr_unload(objhld_t hld);

#endif
