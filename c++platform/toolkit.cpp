#include <cstring>
#include <cstdio>
#include <vector>
#include <atomic>
#include <ctime>
#include <cassert>

#include "zmalloc.h"
#include "toolkit.h"
#include "os_util.hpp"
#include "icom/hash.h"
#include "icom/posix_string.h"
#include "icom/posix_ifos.h"

namespace nsp {
    namespace toolkit {

        template<>
        char *posix_strcpy<char>(char *target, std::size_t cch, const char *src) {
            return ::posix__strcpy(target, cch, src);
        }

        template<>
        wchar_t *posix_strcpy<wchar_t>(wchar_t *target, std::size_t cch, const wchar_t *src) {
            return ::posix__wcscpy(target, cch, src);
        }

        template<>
        char *posix_strdup<char>(const char *src) {
            return ::posix__strdup(src);
        }

        template<>
        wchar_t *posix_strdup<wchar_t>(const wchar_t *src) {
            return ::posix__wcsdup(src);
        }

        template<>
        char *posix_strncpy<char>(char *target, std::size_t cch, const char *src, std::size_t cnt) {
            return ::posix__strncpy(target, cch, src, cnt);
        }

        template<>
        wchar_t *posix_strncpy<wchar_t>(wchar_t *target, std::size_t cch, const wchar_t *src, std::size_t cnt) {
            return ::posix__wcsncpy(target, cch, src, cnt);
        }

        template<>
        char *posix_strcat<char>(char *target, std::size_t cch, const char *src) {
            return ::posix__strcat(target, cch, src);
        }

        template<>
        wchar_t *posix_strcat<wchar_t>(wchar_t *target, std::size_t cch, const wchar_t *src) {
            return ::posix__wcscat(target, cch, src);
        }

        template<>
        char *posix_strrev<char>(char *src) {
            return ::posix__strrev(src);
        }

        template<>
        wchar_t *posix_strrev<wchar_t>(wchar_t *src) {
            return ::posix__wcsrev(src);
        }

        template<>
        int posix_vsnprintf<char>(char *const target, std::size_t cch, const char *format, va_list ap) {
#if _WIN32
            return vsnprintf_s(target, cch, _TRUNCATE, format, ap);
#else
            return vsnprintf(target, cch, format, ap);
#endif
        }

        template<>
        int posix_vsnprintf<wchar_t>(wchar_t *const target, std::size_t cch, const wchar_t *format, va_list ap) {
#if _WIN32
            return _vsnwprintf_s(target, cch, _TRUNCATE, format, ap);
#else
            return vswprintf(target, cch, format, ap);
#endif
        }

        template<>
        int posix_vsprintf<char>(char *const target, std::size_t cch, const char *format, va_list ap) {
            return ::posix__vsprintf(target, cch, format, ap);
        }

        template<>
        int posix_vsprintf<wchar_t>(wchar_t *const target, std::size_t cch, const wchar_t *format, va_list ap) {
            return ::posix__vswprintf(target, cch, format, ap);
        }

        template<>
        int posix_strcasecmp<char>(const char *s1, const char *s2) {
            return ::posix__strcasecmp(s1, s2);
        }

        template<>
        int posix_strcasecmp<wchar_t>(const wchar_t *s1, const wchar_t *s2) {
#if _WIN32
            return _wcsicmp(s1, s2);
#else
            return wcscasecmp(s1, s2);
#endif
        }

        template<>
        char *posix_strtok<char>(char *s, const char *delim, char **save_ptr) {
            return ::posix__strtok(s, delim, save_ptr);
        }

        template<>
        wchar_t *posix_strtok<wchar_t>(wchar_t *s, const wchar_t *delim, wchar_t **save_ptr) {
            return ::posix__wcstok(s, delim, save_ptr);
        }

        template<>
        std::basic_string<char> strformat<char>(int cch, const char *format, ...) {
            char *buffer;
            try {
                buffer = new char[cch + 1];
            } catch (...) {
                return "";
            }

            va_list ap;
            va_start(ap, format);
            int pos = posix_vsnprintf(buffer, cch, format, ap);
            va_end(ap);

            if (pos <= 0 || pos > cch) {
                delete[]buffer;
                return "";
            }
            std::basic_string<char> output(buffer, pos);
            delete[]buffer;
            return output;
        }

        template<>
        std::basic_string<wchar_t> strformat<wchar_t>(int cch, const wchar_t *format, ...) {
            wchar_t *buffer;
            try {
                buffer = new wchar_t[cch + 1];
            } catch (...) {
                return L"";
            }

            va_list ap;
            va_start(ap, format);
            int pos = posix_vsnprintf(buffer, cch, format, ap);
            va_end(ap);

            if (pos <= 0 || pos > cch) {
                delete[]buffer;
                return L"";
            }

            std::basic_string<wchar_t> output(buffer, cch);
            delete[]buffer;

            return output;
        }

        nsp_boolean_t is_digit_str(const std::string &str) {
            std::size_t len = str.length();
            if (0 == len) {
                return NO;
            }
            for (std::size_t i = 0; i < len; i++) {
                if (!isdigit(str[i])) {
                    return NO;
                }
            }
            return YES;
        }

        uint32_t ipv4_touint(const char *ipv4str, int method) {
            return ::posix__ipv4tou(ipv4str, static_cast<enum byte_order_t> (method));
        }

        char *ipv4_tostring(uint32_t ipv4Integer, char * ipv4TextString, uint32_t lengthOfTextCch) {
            return ::posix__ipv4tos(ipv4Integer, ipv4TextString, lengthOfTextCch);
        }

        template<>
        std::string to_string(const double d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), "%.6f", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint16_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), "%u", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int16_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), "%d", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint32_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), "%u", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int32_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), "%d", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint64_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), UINT64_STRFMT, d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int64_t d) {
            char tmp[64];
            ::posix__sprintf(tmp, cchof(tmp), INT64_STRFMT, d);
            return std::string().assign(tmp);
        }

        uint32_t htonl(const uint32_t l) {
            return change_byte_order<uint32_t>(l);
        }

        unsigned short htons(const uint16_t s) {
            return change_byte_order<unsigned short>(s);
        }

        uint32_t ntohl(const uint32_t l) {
            return htonl(l);
        }

        unsigned short ntohs(const uint16_t s) {
            return htons(s);
        }

        // greatest common divisor

        int gcd(int a, int b) {
            if (a < b) {
                int temp = a;
                a = b;
                b = temp;
            }
            return ( (0 == b) ? (a) : gcd(b, a % b));
        }

        // Least common multiple

        double lcm(int a, int b) {
            return (double) ((double) a * b) / gcd(a, b);
        }

        double deg2rad(double angle) {
            return angle * 2 * PI / 360;
        }

        double rad2deg(double radian) {
            return radian * 360 / (2 * PI);
        }

        /*计算离散傅立叶变换*/
        int dispersed_fourier_transform(int dir, int m, double *x1, double *y1) {
            long i, k;
            double arg;
            double cosarg, sinarg;
            double *x2 = nullptr;
            double *y2 = nullptr;
            int retval = 0;

            try {
                x2 = new double[m];
                y2 = new double[m];

                for (i = 0; i < m; i++) {
                    x2[i] = 0;
                    y2[i] = 0;
                    arg = -dir * 2.0 * PI * (double) i / (double) m;

                    // gcc -lm (libm.a | libm.so )
                    for (k = 0; k < m; k++) {
                        cosarg = cos(k * arg);
                        sinarg = sin(k * arg);
                        x2[i] += (x1[k] * cosarg - y1[k] * sinarg);
                        y2[i] += (x1[k] * sinarg + y1[k] * cosarg);
                    }
                }

                /*Copythedataback*/
                if (dir == 1) {
                    for (i = 0; i < m; i++) {
                        x1[i] = x2[i] / (double) m;
                        y1[i] = y2[i] / (double) m;
                    }
                } else {
                    for (i = 0; i < m; i++) {
                        x1[i] = x2[i];
                        y1[i] = y2[i];
                    }
                }
            } catch (...) {
                retval = -1;
            }

            if (x2) {
                delete[]x2;
            }
            if (y2) {
                delete []y2;
            }

            return retval;
            ;
        }

        // 按分隔符切割子串, 实现 strtok 的功能

        template<>
        std::size_t slicing_symbol_string(const std::basic_string<char> &source, const char symbol, std::vector<std::basic_string<char>> &vct_substr) {
            std::basic_string<char> tmp;
            std::size_t idx_previous = 0, idx_found = 0;
            while (std::basic_string<char>::npos != (idx_found = source.find_first_of(symbol, idx_found))) {
                tmp.assign(&source[idx_previous], idx_found - idx_previous);
                if (tmp.size() > 0) {
                    vct_substr.push_back(tmp);
                }
                idx_previous = ++idx_found;
            }
            if (0 == idx_previous) {
                return 0;
            }
            if (source.size() != idx_previous) {
                tmp.assign(&source[idx_previous], source.size() - idx_previous);
                vct_substr.push_back(tmp);
            }
            return vct_substr.size();
        }

        template<>
        std::size_t slicing_symbol_string(const std::basic_string<wchar_t> &source, const wchar_t symbol, std::vector<std::basic_string<wchar_t>> &vct_substr) {
            std::basic_string<wchar_t> tmp;
            std::size_t idx_previous = 0, idx_found = 0;
            while (std::basic_string<wchar_t>::npos != (idx_found = source.find_first_of(symbol, idx_found))) {
                tmp.assign(&source[idx_previous], idx_found - idx_previous);
                if (tmp.size() > 0) {
                    vct_substr.push_back(tmp);
                }
                idx_previous = ++idx_found;
            }
            if (0 == idx_previous) {
                return 0;
            }
            if (source.size() != idx_previous) {
                tmp.assign(&source[idx_previous], source.size() - idx_previous);
                vct_substr.push_back(tmp);
            }
            return vct_substr.size();
        }

        template <>
        char *trim_space(const char *inputString, char * outputString, std::size_t cch) {
            const char * ori;
            char * tag;

            if (!inputString || !outputString || inputString == outputString) return NULL;

            tag = outputString;
            ori = inputString;

            while (0x20 == *ori && 0 != *ori) ori++;


            if (0 == strlen(ori)) {
                outputString[0] = 0;
            } else {
                posix_strcpy(outputString, cch, ori);
            }

            if (0 == strlen(outputString)) return outputString; // 空串, 或全空格的原始串

            tag = &outputString[strlen(outputString) - 1];
            while (0x20 == *tag) {
                *tag = 0;
                tag--;
            }

            return outputString;
        }

        // 去除左右空格
        template<>
        int trim(const std::basic_string<char> &src, std::basic_string<char> &dst) {
            try {
                auto uptr_size = src.size() + 1;
                char *uptr = new char[uptr_size];
                char *p = trim_space(src.c_str(), uptr, uptr_size);
                dst = (p ? p : "");
                delete[]uptr;
            } catch (...) {
                return -1;
            }
            return 0;
        }

        template<>
        void trim(std::basic_string<char> &str) {
            if (0 == str.size()) {
                return;
            }
            try {
                auto uptr_size = str.size() + 1;
                char *uptr = new char[uptr_size];
                char *p = trim_space(str.c_str(), uptr, uptr_size);
                str = (p ? p : "");
                delete[]uptr;
            } catch (...) {
                ;
            }
        }

        // 保留原串，取出左右空格后返回新串

        template<>
        std::basic_string<char> trim_copy(const std::basic_string<char> &src) {
            std::basic_string<char> dst;
            try {
                auto uptr_size = src.size() + 1;
                char *uptr = new char[src.size() + 1];
                std::basic_string<char> tmp = src;
                char *p = trim_space(tmp.c_str(), uptr, uptr_size);
                dst.assign(p);
                delete[]uptr;
            } catch (...) {
                ;
            }
            return dst;
        }

        int random(const int range_min, const int range_max) {
            return ::posix__random(range_min, range_max);
        }

        template<class T>
        void encrypt_key_format(std::basic_string<T> &key) {
            if ((key.size() < 16) || ((key.size() % 8) != 0)) {
                MD5_CTX ctx;
                unsigned char disgest[16];
                ::MD5__Init(&ctx);
                ::MD5__Update(&ctx, (const unsigned char *) key.data(), (int) key.size());
                ::MD5__Final(&ctx, disgest);
                key.clear();
                key.assign((const T *) disgest, 16);
            }
        }

        template<>
        int encrypt(const std::basic_string<char> &origin, const std::basic_string<char> &okey, std::string &out) {

            std::basic_string<char> akey = okey;
            encrypt_key_format(akey);

            unsigned char *output_buffer;
            int outcb;
            int retval = nsp::toolkit::encrypt(
                    (const unsigned char *) origin.data(), (int) origin.size(),
                    (const unsigned char *) akey.data(), (int) akey.size(),
                    &output_buffer, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const char *) output_buffer, outcb);
            zfree(output_buffer);
            return 0;
        }

        template<>
        int encrypt(const std::basic_string<unsigned char> &origin, const std::basic_string<unsigned char> &okey, std::string &out) {
            std::basic_string<unsigned char> akey = okey;
            encrypt_key_format(akey);

            unsigned char *output_buffer;
            int outcb;
            int retval = nsp::toolkit::encrypt(
                    origin.data(), (int) origin.size(),
                    akey.data(), (int) akey.size(),
                    &output_buffer, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const char *) output_buffer, outcb);
            zfree(output_buffer);
            return 0;
        }

        template<>
        int encrypt(const std::vector<char> &origin, const std::vector<char> &okey, std::string &out) {
            std::basic_string<char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            unsigned char *output_buffer;
            int outcb;
            int retval = nsp::toolkit::encrypt(
                    (const unsigned char *) &origin[0], (int) origin.size(),
                    (const unsigned char *) &akey[0], (int) akey.size(),
                    &output_buffer, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const char *) output_buffer, outcb);
            zfree(output_buffer);
            return 0;
        }

        template<>
        int encrypt(const std::vector<unsigned char> &origin, const std::vector<unsigned char> &okey, std::string &out) {
            std::basic_string<unsigned char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            unsigned char *output_buffer;
            int outcb;
            int retval = nsp::toolkit::encrypt(
                    &origin[0], (int) origin.size(),
                    &akey[0], (int) akey.size(),
                    &output_buffer, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const char *) output_buffer, outcb);
            zfree(output_buffer);
            return 0;
        }

        int encrypt(const char *origin, const char *key, std::string &out) {
            if (!origin || !key) {
                return -1;
            }
            return encrypt(std::string(origin, strlen(origin)), std::string(key, strlen(key)), out);
        }

        template<>
        int decrypt(const std::string &crypt, const std::basic_string<char> &okey, std::basic_string<char> &out) {
            std::basic_string<char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            int outcb;
            unsigned char *output;
            int retval = decrypt(
                    (const unsigned char *) crypt.data(), (int) crypt.size(),
                    (const unsigned char *) akey.data(), (int) akey.size(), &output, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const char *) output, outcb);
            zfree(output);
            return 0;
        }

        template<>
        int decrypt(const std::string &crypt, const std::basic_string<unsigned char> &okey, std::basic_string<unsigned char> &out) {

            std::basic_string<unsigned char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            int outcb;
            unsigned char *output;
            int retval = decrypt(
                    (const unsigned char *) crypt.data(), (int) crypt.size(),
                    akey.data(), (int) akey.size(), &output, &outcb);
            if (retval < 0) {
                return -1;
            }

            out.assign((const unsigned char *) output, outcb);
            zfree(output);
            return 0;
        }

        template<>
        int decrypt(const std::string &crypt, const std::vector<char> &okey, std::vector<char> &out) {
            std::basic_string<char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            int outcb;
            unsigned char *output;
            int retval = decrypt(
                    (const unsigned char *) crypt.data(), (int) crypt.size(),
                    (const unsigned char *) akey.data(), (int) akey.size(), &output, &outcb);
            if (retval < 0) {
                return -1;
            }

            for (int i = 0; i < outcb; i++) {
                out.push_back((signed char) output[i]);
            }
            zfree(output);
            return 0;
        }

        template<>
        int decrypt(const std::string &crypt, const std::vector<unsigned char> &okey, std::vector<unsigned char> &out) {
            std::basic_string<unsigned char> akey(&okey[0], okey.size());
            encrypt_key_format(akey);

            int outcb;
            unsigned char *output;
            int retval = decrypt(
                    (const unsigned char *) crypt.data(), (int) crypt.size(),
                    akey.data(), (int) akey.size(), &output, &outcb);
            if (retval < 0) {
                return -1;
            }

            for (int i = 0; i < outcb; i++) {
                out.push_back(output[i]);
            }
            zfree(output);
            return 0;
        }

        int decrypt(const std::string &crypt, const char *key, std::string &out) {
            if (!key) {
                return -1;
            }
            return decrypt(crypt, std::string(key, strlen(key)), out);
        }

        uint32_t crc32(uint32_t crc, const unsigned char *block, uint32_t cb) {
            return ::crc32(crc, block, cb);
        }

        template<>
        void md5<unsigned char>(const unsigned char *input, int inputlen, unsigned char digest[16]) {
            if (!input || 0 == inputlen) {
                return;
            }

            MD5_CTX ctx;
            ::MD5__Init(&ctx);
            ::MD5__Update(&ctx, input, inputlen);
            ::MD5__Final(&ctx, (unsigned char *) &digest[0]);
        }

        template<>
        void md5<char>(const char *input, int inputlen, unsigned char digest[16]) {
            if (!input || 0 == inputlen) {
                return;
            }

            MD5_CTX ctx;
            ::MD5__Init(&ctx);
            ::MD5__Update(&ctx, (const uint8_t *) input, inputlen);
            ::MD5__Final(&ctx, (unsigned char *) &digest[0]);
        }

        int base64_encode(const std::string &input, std::string &output) {
            int outcb;
            char *temp_output = nullptr, *retptr = nullptr;
            const char *inptr;
            int incb;

            try {
                inptr = input.c_str();
                incb = input.size();
                outcb = base64_encode_len(incb);
                if (outcb <= 0) {
                    return -1;
                }

                temp_output = new char[outcb];
                retptr = ::base64_encode(inptr, incb, temp_output);
                if ( retptr ) {
                    output.assign(temp_output, outcb);
                }
            } catch (...) {
                ;
            }

            if (temp_output) {
                delete []temp_output;
            }
            return 0;
        }

        int base64_decode(const std::string &input, std::string &output) {
            int outcb, retval = -1;
            char *temp_output = nullptr;
            const char *inptr;
            int incb;

            try {
                inptr = input.c_str();
                incb = input.size();
                outcb = base64_decode_len(inptr, incb);
                if (outcb <= 0) {
                    return -1;
                }

                temp_output = new char[outcb];
                retval = ::base64_decode(inptr, incb, temp_output);
                if (retval >= 0) {
                    output.assign(temp_output, outcb);
                }
            } catch (...) {
                ;
            }

            if (temp_output) {
                delete[]temp_output;
            }

            return retval;
        }

		template<>
		uint32_t vfn1_hash<uint32_t>(const unsigned char *hash, int length) {
			return vfn1_h32(hash,length);
		}

		template<>
		uint64_t vfn1_hash<uint64_t>(const unsigned char *hash, int length) {
			return vfn1_h64(hash,length);
		}

		template<>
		uint32_t vfn1a_hash<uint32_t>(const unsigned char *hash, int length) {
			return vfn1a_h32(hash,length);
		}

		template<>
		uint64_t vfn1a_hash<uint64_t>(const unsigned char *hash, int length) {
			return vfn1a_h64(hash,length);
		}
        /////////////////////////////////////////// 浮点和 fixed point 的定向转换 ///////////////////////////////

        typedef struct {
            uint16_t u_spare_ : 3; // 对齐
            uint16_t u_sign_ : 12;
            int16_t sign_ : 1;
        } fixed_bit_t;

        typedef union {
            uint16_t fixed_;
            fixed_bit_t fixed_bit_;
        } fixed_t;

        typedef struct {
            uint32_t u_significand : 23; // 有效数据段
            int exponent_ : 8; // 运算指数
            int sign_ : 1; // 符号位
        } float_bit_t;

        typedef union {
            float as_float_;
            float_bit_t as_float_bits_;
            unsigned int as_uint_;
        } float_t;

        unsigned short float2fixed(float fFloat) {
            float_t parser;
            unsigned int float_as_integer;
            int sign;
            int exponent;
            int significand;

            parser.as_float_ = fFloat;
            float_as_integer = parser.as_uint_;

            sign = (int) float_as_integer < 0;
            exponent = ((float_as_integer & 0x7fffffff) >> 23) - 0x7f;
            significand = (float_as_integer & 0x7fffff) + 0x800000;

            // float的significand表示1~2之间的数，expoent=0x7f表示指数0
            // 指数溢出
            assert((exponent >= -12) && (exponent <= 0));

            significand >>= (8 - exponent);
            significand &= 0xfffff8; // 清除低3位

            if (sign == 0) {
                return (unsigned short) significand;
            } else {
                return (unsigned short) significand | 0x8000;
            }
        }

        float fixed2float(unsigned short uFixed) {
            float_t parser;
            int sign;
            int exponent;
            int significand;

            sign = (short) uFixed < 0;
            exponent = 0x7f;
            significand = (uFixed & 0x7fff) << 8;

            while ((significand & 0x800000) == 0) {
                significand <<= 1;
                exponent--;
            }
            significand -= 0x800000;
            parser.as_uint_ = (sign << 31) | (exponent << 23) | significand;
            return parser.as_float_;
        }

    } // toolkit
} // nsp
