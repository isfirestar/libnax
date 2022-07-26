#include "naos.h"

#include "abuff.h"
#include "zmalloc.h"

PORTABLEIMPL(nsp_status_t) naos_ipv4tos(uint32_t inet, abuff_naos_inet_t *inetstr)
{
    unsigned char inetby[4];
    int i;

	if ( unlikely((!inetstr)) ) {
        return posix__makeerror(EINVAL);
    }

    for (i = 0; i < sizeof(inetby); i++) {
		inetby[i] = (unsigned char)(inet & 0xFF);
		inet >>= 8;
    }

	abuff_sprintf(inetstr, "%hhu.%hhu.%hhu.%hhu", inetby[3], inetby[2], inetby[1], inetby[0]);
	return NSP_STATUS_SUCCESSFUL;
}

PORTABLEIMPL(uint32_t) naos_ipv4tou(const char *inetstr, enum byte_order_t method)
{
    static const int BIT_MOV_FOR_LITTLE_ENDIAN[4] = {24, 16, 8, 0};
    static const int BIT_MOV_FOR_BIG_ENDIAN[4] = {0, 8, 16, 24};
    char *p;
    unsigned long byteValue;
    unsigned long digital;
    char *nextToken;
    int i;
    char *tmpstr;
    size_t srclen;

	if ( unlikely(!inetstr) || unlikely( ((method != kByteOrder_LittleEndian) && (method != kByteOrder_BigEndian)))) {
		return 0;
	}

    if (!naos_is_legal_ipv4(inetstr)) {
        return 0;
    }

    srclen = strlen(inetstr);
    digital = 0;
    i = 0;

    tmpstr = (char *)ztrymalloc(srclen + 1);
    if ( unlikely(!tmpstr) ) {
        return 0;
    }
    crt_strcpy(tmpstr, srclen + 1, inetstr);

    nextToken = NULL;
    while (NULL != (p = crt_strtok(nextToken ? NULL : tmpstr, ".", &nextToken)) && i < 4) {
        byteValue = strtoul(p, NULL, 10);
        digital |= byteValue << (kByteOrder_LittleEndian == method ? BIT_MOV_FOR_LITTLE_ENDIAN : BIT_MOV_FOR_BIG_ENDIAN)[i];
        ++i;
    }

    zfree(tmpstr);
    return digital;
}

PORTABLEIMPL(uint32_t) naos_chord32(uint32_t value)
{
    uint32_t dst;
    int i;

    dst = 0;
    for (i = 0; i < sizeof ( value); i++) {
        dst |= ((value >> (i * BITS_P_BYTE)) & 0xFF);
        dst <<= ((i) < (sizeof ( value) - 1) ? BITS_P_BYTE : (0));
    }
    return dst;
}

PORTABLEIMPL(uint16_t) naos_chord16(uint16_t value)
{
    uint16_t dst = 0;
    int i;

    for (i = 0; i < sizeof ( value); i++) {
        dst |= ((value >> (i * BITS_P_BYTE)) & 0xFF);
        dst <<= ((i) < (sizeof ( value) - 1) ? BITS_P_BYTE : (0));
    }
    return dst;
}

PORTABLEIMPL(nsp_boolean_t) naos_is_legal_ipv4(const char *inetstr)
{
    const char *cursor;
    int i, j, k;
    char segm[4][4];

    if ( unlikely(!inetstr) ) {
        return nsp_false;
    }

    cursor = inetstr;
    i = j = k = 0;
    memset(segm, 0, sizeof(segm));

    while (*cursor) {
        /* "255.255.255.255." ||  "192.168.1.0.1" */
        if ( ((INET_ADDRSTRLEN - 1) == i) || (k > 3) ) {
            return nsp_false;
        }

        if (*cursor == '.' ) {
            /* ".192.168.2.2 or 192..168.0.1" || "256.1.1.0" */
            if ( (0 == segm[k]) || ((atoi(segm[k]) > MAX_UINT8)) ) {
                return nsp_false;
            }

            cursor++;
            i++;
            k++;
            j = 0;
            continue;
        }

        if (*cursor >= '0' && *cursor <= '9' ) {
            if (j >= 3) {  /* 1922.0.0.1 */
                return nsp_false;
            }
            segm[k][j] = *cursor;
            cursor++;
            i++;
            j++;
            continue;
        }

        /* any other characters */
        return nsp_false;
    }

    /* "192.168" || "192.168.0."" */
    if ( (3 != k) || (0 == j) ) {
        return nsp_false;
    }

    /* 255.255.255.256 */
    if ((atoi(segm[k]) > MAX_UINT8)) {
        return nsp_false;
    }

    return nsp_true;
}

PORTABLEIMPL(void) naos_hexdump(const unsigned char *buffer, uint16_t length, uint8_t columns, void (*on_dump)(const char *text, uint32_t length))
{
    char *display, *p;
    uint32_t display_length;
    uint8_t col;
    uint32_t offset;
    uint32_t i;
    int written;

    if ( !buffer || 0 == length) {
        return;
    }

    display_length = length * 3 + length / columns + 8;
    display = (char *)ztrymalloc(display_length);
    if (!display) {
        return;
    }
    p = &display[0];
    col = 0;
    offset = 0;

    offset += snprintf(p + offset, display_length - offset , "\n");
    for (i = 0; i < length; i++, col++) {
        if (col == columns) {
            written = snprintf(p + offset, display_length - offset, "\n");
            if (written >= display_length - offset) {
                break;
            }
            offset += written;
            col = 0;
        }
        written = snprintf(p + offset, display_length - offset, " %02X", buffer[i]);
        if (written >= display_length - offset) {
            break;
        }
        offset += written;
    }

    /* callback to user context */
    if ( on_dump) {
        on_dump(display, offset);
    } else {
        printf(display);
    }
    zfree(display);
}
