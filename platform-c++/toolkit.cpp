#include <cstring>
#include <cstdio>
#include <vector>
#include <atomic>
#include <ctime>
#include <cassert>

#include "toolkit.h"
#include "os_util.hpp"

#include "hash.h"
#include "ifos.h"
#include "naos.h"

namespace nsp {
    namespace toolkit {

        template<>
        std::basic_string<char> strformat<char>(int cch, const char *format, ...)
        {
            char *buffer;
            try {
                buffer = new char[cch + 1];
            } catch (...) {
                return "";
            }

            va_list ap;
            va_start(ap, format);
            int pos = crt_vsnprintf(buffer, cch, format, ap);
            va_end(ap);

            if (pos <= 0 || pos > cch) {
                delete[]buffer;
                return "";
            }
            std::basic_string<char> output(buffer, pos);
            delete[]buffer;
            return output;
        }

        nsp_boolean_t is_digit_str(const std::string &str)
        {
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

        template<>
        std::string to_string(const double d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), "%.6f", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint16_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), "%u", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int16_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), "%d", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint32_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), "%u", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int32_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), "%d", d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const uint64_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), UINT64_STRFMT, d);
            return std::string().assign(tmp);
        }

        template<>
        std::string to_string(const int64_t d)
        {
            char tmp[64];
            crt_sprintf(tmp, cchof(tmp), INT64_STRFMT, d);
            return std::string().assign(tmp);
        }

        uint32_t htonl(const uint32_t l)
        {
            return change_byte_order<uint32_t>(l);
        }

        unsigned short htons(const uint16_t s)
        {
            return change_byte_order<unsigned short>(s);
        }

        uint32_t ntohl(const uint32_t l) {
            return htonl(l);
        }

        unsigned short ntohs(const uint16_t s)
        {
            return htons(s);
        }

        // greatest common divisor
        int gcd(int a, int b)
        {
            if (a < b) {
                int temp = a;
                a = b;
                b = temp;
            }
            return ( (0 == b) ? (a) : gcd(b, a % b));
        }

        // Least common multiple

        double lcm(int a, int b)
        {
            return (double) ((double) a * b) / gcd(a, b);
        }

        double deg2rad(double angle)
        {
            return angle * 2 * PI / 360;
        }

        double rad2deg(double radian)
        {
            return radian * 360 / (2 * PI);
        }

        /*计算离散傅立叶变换*/
        int dispersed_fourier_transform(int dir, int m, double *x1, double *y1)
        {
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
        std::size_t slicing_symbol_string(const std::basic_string<char> &source, const char symbol, std::vector<std::basic_string<char>> &vct_substr)
        {
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
        std::size_t slicing_symbol_string(const std::basic_string<wchar_t> &source, const wchar_t symbol,
            std::vector<std::basic_string<wchar_t>> &vct_substr)
        {
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
        char *trim_space(const char *inputString, char * outputString, std::size_t cch)
        {
            const char * ori;
            char * tag;

            if (!inputString || !outputString || inputString == outputString) return NULL;

            tag = outputString;
            ori = inputString;

            while (0x20 == *ori && 0 != *ori) ori++;


            if (0 == strlen(ori)) {
                outputString[0] = 0;
            } else {
                crt_strcpy(outputString, cch, ori);
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
        int trim(const std::basic_string<char> &src, std::basic_string<char> &dst)
        {
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
        void trim(std::basic_string<char> &str)
        {
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
        std::basic_string<char> trim_copy(const std::basic_string<char> &src)
        {
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

        int random(const int range_min, const int range_max)
        {
            return ::ifos_random(range_min, range_max);
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

        unsigned short float2fixed(float fFloat)
        {
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

        float fixed2float(unsigned short uFixed)
        {
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
