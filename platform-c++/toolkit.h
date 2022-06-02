#if !defined TOOLKIT_HEADER_02160613
#define TOOLKIT_HEADER_02160613

#include "compiler.h"
#include "abuff.h"

#include <vector>
#include <string>

namespace nsp {
    namespace toolkit {

        // 将 @n 提升到下一个2的正整次幂
        // 如果正数 @n 不是 2 的正整次幂，只需找到其最高的有效位1所在的位置（从1开始计数）pos，然后1 << pos即可将k向上取整为2的正整次幂

        template<class T>
        inline T roundup_powerof_2(T n) {
            if (is_powerof_2(n)) {
                return n;
            }
            // 至少保证 2 的 1 次幂
            if (0 == n) {
                return 2;
            }
            uint32_t position = 0;
            for (int i = n; i != 0; i >>= 1) {
                position++;
            }
            return static_cast<T> ((T) 1 << position);
        }

        nsp_boolean_t is_digit_str(const std::string &str);

        template<class T>
        std::basic_string<T> to_string(const double d);
        template<class T>
        std::basic_string<T> to_string(const uint16_t d);
        template<class T>
        std::basic_string<T> to_string(const int16_t d);
        template<class T>
        std::basic_string<T> to_string(const uint32_t d);
        template<class T>
        std::basic_string<T> to_string(const int32_t d);
        template<class T>
        std::basic_string<T> to_string(const uint64_t d);
        template<class T>
        std::basic_string<T> to_string(const int64_t d);

        template<class T>
        std::basic_string<T> strformat(int cch, const T *format, ...);

        // 字节序转换例程

        template<class T>
        T change_byte_order(const T &t) {
            T dst = 0;
            for (unsigned int i = 0; i < sizeof ( T); i++) {
                // dst = (dst | (t >> (i * BITS_P_BYTE)) & 0xFF) << ((i < (sizeof ( T) - 1) ? BITS_P_BYTE : 0));
                dst |= ((t >> (i * BITS_P_BYTE)) & 0xFF);
                dst <<= ((i) < (sizeof ( T) - 1) ? BITS_P_BYTE : (0));
            }
            return dst;
        }
        uint32_t htonl(const uint32_t l);
        unsigned short htons(const uint16_t s);
        uint32_t ntohl(const uint32_t l);
        unsigned short ntohs(const uint16_t s);

        template<class T>
        T *trim_space(const T *inputString, T * outputString, std::size_t cch);

        // 最大公约数, greatest common divisor
        int gcd(int a, int b);
        // 最小公倍数, Least common multiple
        double lcm(int a, int b);
        // 角度转弧度
        double deg2rad(double angle);
        // 弧度转角度
        double rad2deg(double radian);
        // 计算离散傅立叶
        int dispersed_fourier_transform(int dir, int m, double *x1, double *y1);

        // 按分隔符切割子串
        template<class T>
        std::size_t slicing_symbol_string(const std::basic_string<T> &source, const T symbol, std::vector<std::basic_string<T>> &vct_substr);

        // 去除左右空格
        template<class T>
        int trim(const std::basic_string<T> &src, std::basic_string<T> &dst);
        template<class T>
        void trim(std::basic_string<T> &str);

        // 保留原串，取出左右空格后返回新串
        template<class T>
        std::basic_string<T> trim_copy(const std::basic_string<T> &src);

        // 取随机数 [range_min, range_max)
        // 如果参数为 (0,0), 则范围为默认的[0, 32768)
        // 如果 range_max < range_min, 做默认处理， 范围不生效
        // 如果 range_max == range_min, 且非0， 则返回该值
        int random(const int range_min = 0, const int range_max = 0);

        // HEX字符串转整型, "1234ABCD" 转换为 0x1234ABCD， 注意高低位结构

        template<class T>
        int strtohex(const std::basic_string<char> &strhex, T &int_hex) {
            // 输出初始值
            int_hex = 0;

            // 权值
            int access = strhex.length() - 1;

            // 超出了描述范围
            if (strhex.length() > (sizeof ( T) * 2)) {
                return -1;
            }

            for (std::size_t i = 0; i < strhex.length(); i++) {
                char c = strhex.at(i);
                if (c >= 0x30 && c <= 0x39) {
                    int_hex += (T) ((c - 0x30) * ((access > 0) ? pow((double) 16.0, access) : 1));
                } else {
                    if (c >= 0x41 && c <= 0x46) {
                        int_hex += (T) ((c - 0x37) * ((access > 0) ? pow((double) 16.0, access) : 1));
                    } else if (c >= 0x61 && c <= 0x66) {
                        int_hex += (T) ((c - 0x57) * ((access > 0) ? pow((double) 16.0, access) : 1));
                    } else {
                        return -1;
                    }
                }
                access--;
            }

            return 0;
        }

        // 浮点转换
        unsigned short float2fixed(float fFloat);
        float fixed2float(unsigned short uFixed);

    } // toolkit
} // nsp

#endif
