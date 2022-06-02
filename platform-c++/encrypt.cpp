#include <cstring>
#include <cstdlib>

namespace nsp {
    namespace toolkit {
        static const unsigned char ENCODE_TABLE[] = {
            0x32, 0x31, 0x30, 0x36, 0x46, 0x34, 0x35, 0x33, 0x45, 0x38, 0x37, 0x39, 0x43, 0x41, 0x42, 0x44,
            0x49, 0x48, 0x47, 0x4D, 0x56, 0x4B, 0x4C, 0x4A, 0x55, 0x4F, 0x4E, 0x50, 0x53, 0x51, 0x52, 0x54,
            0x59, 0x58, 0x57, 0x63, 0x6C, 0x61, 0x62, 0x5A, 0x6B, 0x65, 0x64, 0x66, 0x69, 0x67, 0x68, 0x6A,
            0x6F, 0x6E, 0x6D, 0x73, 0x28, 0x71, 0x72, 0x70, 0x27, 0x75, 0x74, 0x76, 0x79, 0x77, 0x78, 0x7A
        };

        static const char DECODE_TABLE[] = {
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*0-15*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*16-31*/
            -1, -1, -1, -1, -1, -1, -1, 56, 52, -1, -1, -1, -1, -1, -1, -1, /*32-47*/
            2, 1, 0, 7, 5, 6, 3, 10, 9, 11, -1, -1, -1, -1, -1, -1, /*48-63*/
            -1, 13, 14, 12, 15, 8, 4, 18, 17, 16, 23, 21, 22, 19, 26, 25, /*64-79*/
            27, 29, 30, 28, 31, 24, 20, 34, 33, 32, 39, -1, -1, -1, -1, -1, /*80-95*/
            -1, 37, 38, 35, 42, 41, 43, 45, 46, 44, 47, 40, 36, 50, 49, 48, /*96-111*/
            55, 53, 54, 51, 58, 57, 59, 61, 62, 60, 63, -1, -1, -1, -1, -1, /*112-127*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*128-143*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*144-159*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*160-175*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*176-191*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*192-207*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*208-223*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, /*224-239*/
            -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 /*240-255*/
        };

        int encrypt(const unsigned char* ori, int oricb, const unsigned char* key, int keycb, unsigned char** out, int* outcb) {
            int blend_len = 2 * oricb;
            int src_len = 0;
            int symbol_add = 0;
            int page = 0;
            int dstlen = 0;
            unsigned char* blend;
            unsigned char* src_buffer;
            unsigned char* des_buffer;
            unsigned char* output_buffer;
            int i;
            int j;

            if (!ori || oricb <= 0 || !key || keycb < 8 || !outcb || !out) {
                return -1;
            }

            blend = (unsigned char *)malloc(blend_len);
            if (!blend) {
                return -1;
            }

            for (i = 0; i < oricb; i++) {
                unsigned char first_ch = (ori[i] & 0xAA) | (key[i % keycb] & 0x55);
                unsigned char second_ch = (ori[i] & 0x55) | (key[i % keycb] & 0xAA);
                blend[2 * i] = first_ch;
                blend[2 * i + 1] = second_ch;
            }

            if (0 == blend_len % 3) {
                src_len = blend_len;
            } else {
                symbol_add = 3 - (blend_len % 3);
                src_len = blend_len + symbol_add;
            }
            page = src_len / 3;
            src_buffer = (unsigned char *)malloc(src_len);
            if (!src_buffer) {
                free(blend);
                return -1;
            }
            memset(src_buffer, 0, src_len);
            memcpy(src_buffer, blend, blend_len);
            dstlen = page * 4;
            des_buffer = (unsigned char *)malloc(dstlen);
            if (!des_buffer) {
                free(blend);
                free(src_buffer);
                return -1;
            }

            for (i = 0; i < page; i++) {
                unsigned char src_tmp[3];
                unsigned char des_tmp[4];

                memcpy(src_tmp, &src_buffer[i * 3], 3);
                des_tmp[0] = src_tmp[0] >> 2;
                des_tmp[1] = ((src_tmp[0] & 0x03) << 4) | (src_tmp[1] >> 4);
                des_tmp[2] = ((src_tmp[1] & 0x0f) << 2) | (src_tmp[2] >> 6);
                des_tmp[3] = src_tmp[2] & 0x3f;
                for (j = 0; j < 4; j++) {
                    des_tmp[j] = *(ENCODE_TABLE + des_tmp[j]);
                }

                memcpy(&des_buffer[i * 4], des_tmp, 4);
            }

            for (i = 1; i <= symbol_add; i++) {
                des_buffer[dstlen - i] = 0x7E;
            }

            output_buffer = (unsigned char *)malloc(dstlen);
            if (!output_buffer) {
                free(blend);
                free(src_buffer);
                free(des_buffer);
                return -1;
            }
            memset(output_buffer, 0, dstlen);
            memcpy(output_buffer, des_buffer, dstlen);
            *out = output_buffer;
            *outcb = dstlen;

            free(blend);
            free(src_buffer);
            free(des_buffer);
            return 0;
        }

        int decrypt(const unsigned char* crypt, int oricb, const unsigned char* key, int keycb, unsigned char** out, int* outcb) {
            int page = oricb / 4;
            int dstlen = page * 3;
            int output_cb = 0;
            unsigned char* des_buffer;
            unsigned char* output_buffer;
            int i;
            int t;
            int j;
            int output_count;

            if (!crypt || oricb == 0 || (oricb % 4 != 0) || !key || keycb < 8) {
                return -1;
            }

            des_buffer = (unsigned char *)malloc(dstlen);
            if (!des_buffer) {
                return -1;
            }

            for (i = 0, t = 0; i < page; i++, t++) {
                unsigned char src[4];
                memcpy(src, &crypt[i * 4], 4);

                for (j = 0; j < 4; j++) {
                    if ((src[j] >= sizeof ( DECODE_TABLE) / sizeof ( DECODE_TABLE[0])) || src[j] < 0) {
                        free(des_buffer);
                        return -1;
                    }
                    if (src[j] != 0x7E) {
                        src[j] = *(DECODE_TABLE + src[j]);
                        if (-1 == src[j]) {
                            free(des_buffer);
                            return -1;
                        }
                    }
                }

                unsigned char des[3];
                des[0] = (src[0] << 2) | ((src[1] & 0x30) >> 4);
                des[1] = ((src[1] & 0x0f) << 4);
                if (0x7E != src[2]) {
                    des[1] |= (src[2] & 0x3C) >> 2;
                    des[2] = (src[2] << 6);
                } else {
                    dstlen--;
                }
                if (0x7E != src[3]) {
                    des[2] |= src[3];
                } else {
                    dstlen--;
                }
                memcpy(&des_buffer[t * 3], des, 3);
            }

            if (0 != dstlen % 2) {
                free(des_buffer);
                return -1;
            }

            output_cb = dstlen / 2;
            output_buffer = (unsigned char *)malloc(output_cb);
            for (output_count = 0; output_count < output_cb; output_count++) {
                unsigned char out_ctr = (des_buffer[output_count * 2] & 0xAA) | (des_buffer[output_count * 2 + 1] & 0x55);
                unsigned char key_ctr = (des_buffer[output_count * 2] & 0x55) | (des_buffer[output_count * 2 + 1] & 0xAA);
                if (key_ctr != (unsigned char) key[output_count % keycb]) {
                    free(des_buffer);
                    free(output_buffer);
                    return -1;
                }
                memcpy(&output_buffer[output_count], &out_ctr, 1);
            }
            *out = output_buffer;
            *outcb = output_cb;

            free(des_buffer);
            return 0;
        }

    }
}
