#if !defined ENDPOINT_H
#define ENDPOINT_H

#include <string>
#include <cstdint>

#include "compiler.h"

#if !defined MAXIMUM_IPV4_STRLEN
#define MAXIMUM_IPV4_STRLEN     (0x10)
#endif // !MAXIMUM_IPV4_STRLEN

#if !defined MAXIMU_TCPIP_PORT_NUMBER
#define MAXIMU_TCPIP_PORT_NUMBER   (0xFFFF)
#endif // !MAXIMU_TCPIP_PORT_NUMBER

#define INVALID_TCPIP_PORT_NUMBER   (MAXIMU_TCPIP_PORT_NUMBER)

#define BOARDCAST_TCPIP_ADDRESS    (0xFFFFFFFF)
#define MANUAL_NOTIFY_TARGET    (BOARDCAST_TCPIP_ADDRESS)

#define INVALID_ENDPOINT_STD    ("0.0.0.0:65535")

namespace nsp {
    namespace tcpip {

        typedef uint16_t port_t;
        typedef uint32_t u32_ipv4_t;

        class endpoint {
        public:
            endpoint();
            endpoint(const char *ipstr, const port_t po);
            endpoint(const endpoint &rf);
            endpoint &operator=(const endpoint &rf);
            endpoint(endpoint &&rf);
            endpoint &operator=(endpoint &&rf);

        public:
            bool operator==(const endpoint &rf) const; // 允许 endpoint 作为 std::find 对象
            bool operator<(const endpoint &rf) const; // 允许 endpoint 直接作为 std::map 的 KEY 值
            operator bool() const; // 0.0.0.0:65535 将被认为是无效的IP地址, 255.255.255.255:65535作为手动地址有效
            const bool connectable() const; // 可作为 TcpConnect 对象的地址结构
            const bool bindable() const; // 可作为本地绑定
            const bool manual() const; // 延迟确定具体地址信息的对象

        public:
            const char *ipv4() const;
            const u32_ipv4_t ipv4_uint32() const;
            void ipv4(const u32_ipv4_t uint32_address);
            void ipv4(const std::string &ipstr); // 因为已经有 uint32 参数的函数, 必须避免出现 const char * 的重载, 函数失败则保留原有的IP地址串
            void ipv4(const char *ipstr, int cpcch);
            const port_t port() const;
            void port(const port_t po);
            const std::string to_string() const;
            void disable(); // 将对象置为无效

        public:
            // 释义: 为什么不提供 epstr 作为参数的 endpoint 构造函数
            // epstr 的解析不一定能成功, 如果提供构造函数, 将没有任何机会返回异常, 最多只能抛出异常, 增加了客户代码捕获异常的复杂度
            static int build(const std::string &epstr, endpoint &ep);
            static int build(const char *ipstr, uint16_t port, endpoint &ep);
            static endpoint boardcast(const port_t po);

        public:
            static int parse_ep(const std::string & epstr, std::string &ipv4, port_t &port);
            static int parse_domain(const std::string &domain, std::string &ipv4);
            static nsp_boolean_t is_effective_ipv4(const std::string &ipstr);
            static nsp_boolean_t is_effective_port(const std::string &portstr, uint16_t &port);
        private:
            char ipstr_[MAXIMUM_IPV4_STRLEN];
            port_t port_;
            u32_ipv4_t address_;
        };

    } // namespace tcpip
} // nsp

#endif
