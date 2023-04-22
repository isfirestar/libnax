
#include <memory>
#include <mutex>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <map>
#include <errno.h>

#include "network_handler.h"
#include "swnet.h"

#include "logger.h"

namespace nsp {
    namespace tcpip {
        ///////////////////////////////////////		TCP 部分 ///////////////////////////////////////
        static std::atomic_long __tcp_refcnt{ 0}; // GCC4.8中， "= 0"这样的代码被视为废弃

        obtcp::obtcp()
        {
            if (1 == ++__tcp_refcnt) {
                ::tcp_init2(0);
            } else {
                --__tcp_refcnt;
            }
        }

        obtcp::obtcp(const HTCPLINK lnk)
        {
            lnk_ = lnk;
        }

        obtcp::~obtcp()
        {
            close();
        }

        void obtcp::settst(tst_t *tst)
        {
            if (INVALID_HTCPLINK != lnk_ && tst) {
                ::nis_cntl(lnk_, NI_SETTST, tst);
            }
        }

        nsp_status_t obtcp::create(const char *epstr)
        {
            if (!epstr) {
                return posix__makeerror(EINVAL);
            }

            if (0 == rtl_strncasecmp(epstr, "ipc:", 4)) {
                auto status = nsp::toolkit::singleton<swnet>::instance()->tcp_create(shared_from_this(), epstr, 0);
                if (!NSP_SUCCESS(status)) {
                    return status;
                }
                return ::nis_cntl(lnk_, NI_SETTST, &tst_);
            }

            endpoint ep;
            auto status = endpoint::build(epstr, ep);
            if (NSP_SUCCESS(status)) {
                return create(ep);
            }

            return status;
        }

        nsp_status_t obtcp::create(const endpoint &ep)
        {
            if (INVALID_HTCPLINK != lnk_) {
                return posix__makeerror(EINVAL);
            }

            nsp_status_t status;
            std::string ipstr = ep.ipv4();
            try {
                status = nsp::toolkit::singleton<swnet>::instance()->tcp_create(
                        shared_from_this(), ipstr.size() > 0 ? ipstr.c_str() : nullptr, ep.port());
                if (!NSP_SUCCESS(status)) {
                    return status;
                }
            } catch (...) {
                return NSP_STATUS_FATAL;
            }

            return ::nis_cntl(lnk_, NI_SETTST, &tst_);
        }

        nsp_status_t obtcp::create()
        {
            return create(endpoint("0.0.0.0", 0));
        }

        std::weak_ptr<obtcp> obtcp::attach()
        {
            try {
                auto sptr = shared_from_this();
                if (nsp::toolkit::singleton<swnet>::instance()->tcp_attach(lnk_, sptr) < 0) {
                    return std::weak_ptr<obtcp>();
                }

                // 客户连接, 需要拿到对端和本地的地址信息
                uint32_t actip;
                port_t actport;
                ::tcp_getaddr(lnk_, LINK_ADDR_REMOTE, &actip, &actport);
                remote_.ipv4(actip);
                remote_.port(actport);

                ::tcp_getaddr(lnk_, LINK_ADDR_LOCAL, &actip, &actport);
                local_.ipv4(actip);
                local_.port(actport);

                return std::weak_ptr<obtcp>(sptr);
            } catch (...) {
                ;
            }

            return std::weak_ptr<obtcp>();
        }

        void obtcp::close()
        {
            if (INVALID_HTCPLINK != lnk_) {
                ::tcp_destroy(lnk_);
            }
        }

        nsp_status_t obtcp::connect(const char *epstr)
        {
            if (INVALID_HTCPLINK == lnk_) {
                return NSP_STATUS_FATAL;
            }

            if (!epstr) {
                return posix__makeerror(EINVAL);
            }

            nsp_status_t status;
            if (0 == rtl_strncasecmp(epstr, "ipc:", 4)) {
                status = ::tcp_connect(lnk_, epstr, 0);
                if (!NSP_SUCCESS(status)) {
                    return status;
                }
                remote_.ipv4("");
                remote_.port(0);
                local_.ipv4("");
                local_.port(0);
                return status;
            }

            endpoint ep;
            status = endpoint::build(epstr, ep);
            if (NSP_SUCCESS(status)) {
                status = connect(ep);
            }
            return status;
        }

        nsp_status_t obtcp::connect(const endpoint &ep)
        {
            if (INVALID_HTCPLINK == lnk_) {
                return NSP_STATUS_FATAL;
            }

            std::string ipstr = ep.ipv4();
            port_t port = ep.port();
            if (ipstr.length() <= 0 && port <= 0) {
                return NSP_STATUS_FATAL;
            }
            if (::tcp_connect(lnk_, ipstr.c_str(), port) < 0) {
                return NSP_STATUS_FATAL;
            }

            // 成功连接后, 可以取出对端和本地的地址信息
            uint32_t actip;
            port_t actport;
            ::tcp_getaddr(lnk_, LINK_ADDR_REMOTE, &actip, &actport);
            remote_.ipv4(actip);
            remote_.port(actport);
            ::tcp_getaddr(lnk_, LINK_ADDR_LOCAL, &actip, &actport);
            local_.ipv4(actip);
            local_.port(actport);
            return NSP_STATUS_SUCCESSFUL;
        }

		nsp_status_t obtcp::connect2(const char *epstr)
        {
			if (INVALID_HTCPLINK == lnk_) {
				return NSP_STATUS_FATAL;
			}

			if (epstr) {
				endpoint ep;
				if (endpoint::build(epstr, ep) >= 0) {
					return connect2(ep);
				}
			}
			return NSP_STATUS_FATAL;
		}

		nsp_status_t obtcp::connect2(const endpoint &ep)
        {
			if (INVALID_HTCPLINK == lnk_) {
				return NSP_STATUS_FATAL;
			}

			std::string ipstr = ep.ipv4();
			port_t port = ep.port();
			if (ipstr.length() <= 0 && port <= 0) {
				return NSP_STATUS_FATAL;
			}
			if (::tcp_connect2(lnk_, ipstr.c_str(), port) < 0) {
				return NSP_STATUS_FATAL;
			}

			return NSP_STATUS_SUCCESSFUL;
		}

        nsp_status_t obtcp::listen()
        {
            if (INVALID_HTCPLINK == lnk_) {
                return NSP_STATUS_FATAL;
            }

            if (::tcp_listen(lnk_, 5) < 0) {
                return NSP_STATUS_FATAL;
            }

            // 开始监听后, 肯定不会再出现对端地址
            // 并取出本地地址
            remote_.ipv4("0.0.0.0");
            remote_.port(0);
            uint32_t actip;
            port_t actport;
            ::tcp_getaddr(lnk_, LINK_ADDR_LOCAL, &actip, &actport);
            local_.ipv4(actip);
            local_.port(actport);
            return NSP_STATUS_SUCCESSFUL;
        }

        // 直接发送或者组包发送的基本方法
        nsp_status_t obtcp::send(const unsigned char *data, int cb)
        {
            if (INVALID_HTCPLINK != lnk_ && cb > 0 && data) {
                return ::tcp_write(lnk_, data, cb, NULL);
            }
            return NSP_STATUS_FATAL;
        }

        nsp_status_t obtcp::send(const void *origin, int cb, const nis_serializer_fp serializer)
        {
            if (INVALID_HTCPLINK != lnk_ && cb > 0 && origin && serializer) {
                return ::tcp_write(lnk_, origin, cb, serializer);
            }
            return NSP_STATUS_FATAL;
        }

        const endpoint &obtcp::local() const
        {
            return local_;
        }

        const endpoint &obtcp::remote() const
        {
            return remote_;
        }

        void obtcp::on_pre_close(void *context)
        {
            ;
        }

        void obtcp::on_closed(HTCPLINK previous)
        {
            ;
        }

        void obtcp::on_closed()
        {
            HTCPLINK previous = lnk_.exchange(INVALID_HTCPLINK);
            if (INVALID_HTCPLINK != previous) {
                nsp::toolkit::singleton<swnet>::instance()->tcp_detach(previous);
                on_closed(previous);
            }
        }

        void obtcp::on_connected2()
        {
            // when connected by asynchronous, get address information
            uint32_t actip;
            port_t actport;
            ::tcp_getaddr(lnk_, LINK_ADDR_REMOTE, &actip, &actport);
            remote_.ipv4(actip);
            remote_.port(actport);
            ::tcp_getaddr(lnk_, LINK_ADDR_LOCAL, &actip, &actport);
            local_.ipv4(actip);
            local_.port(actport);

            this->on_connected();
        }

        void obtcp::on_recvdata(const std::basic_string<unsigned char> &pkt)
        {
            ;
        }

        void obtcp::on_recvdata(const unsigned char *data, const int cb)
        {
            if (data && cb > 0) {
                on_recvdata(std::basic_string<unsigned char>(data, cb));
            }
        }

        void obtcp::on_accepted(HTCPLINK lnk)
        {
            ;
        }

        void obtcp::on_accepted(HTCPLINK srv, HTCPLINK client)
        {
            if (INVALID_HTCPLINK != srv && INVALID_HTCPLINK != client && srv == lnk_) {
                on_accepted(client);
            }
        }

		void obtcp::on_connected()
        {
			;
        }

        void obtcp::bind_object(const std::shared_ptr<obtcp> &object)
        {
			;
        }

        void obtcp::setlnk(const HTCPLINK lnk)
        {
            lnk_ = lnk;
        }

        ///////////////////////////////////////		UDP 部分 ///////////////////////////////////////
        static int __udp_refcnt = 0;

        obudp::obudp()
        {
            if (1 == ++__udp_refcnt) {
                ::udp_init2(0);
            } else {
                --__udp_refcnt;
            }
        }

        obudp::~obudp()
        {
            close();
        }

        nsp_status_t obudp::create(const int flag)
        {
            return create(endpoint("0.0.0.0", 0), flag);
        }

        nsp_status_t obudp::create(const endpoint &ep, const int flag)
        {
            std::string ipstr = ep.ipv4();
            port_t port = ep.port();

            if (INVALID_HUDPLINK != lnk_) {
                return NSP_STATUS_FATAL;
            }

            try {
                nsp_status_t status = nsp::toolkit::singleton<swnet>::instance()->
                        udp_create(shared_from_this(), ipstr.size() > 0 ? ipstr.c_str() : nullptr, port, flag);
                if (!NSP_SUCCESS(status)) {
                    return status;
                }
            } catch (...) {
                return NSP_STATUS_FATAL;
            }

            uint32_t actip;
            port_t actport;
            ::udp_getaddr(lnk_, &actip, &actport);
            local_.ipv4(actip);
            local_.port(actport);
            return NSP_STATUS_SUCCESSFUL;
        }

        nsp_status_t obudp::create(const char *epstr, const int flag)
        {
            if (epstr) {
                endpoint ep;
                if (endpoint::build(epstr, ep) >= 0) {
                    return create(ep, flag);
                }
            }
            return NSP_STATUS_FATAL;
        }

        void obudp::close()
        {
            if (INVALID_HUDPLINK != lnk_) {
                ::udp_destroy(lnk_);
            }
        }

        nsp_status_t obudp::sendto(const unsigned char *data, int cb, const endpoint &ep)
        {
            if (INVALID_HUDPLINK != lnk_ && data && cb > 0) {
                return ::udp_write(lnk_, data, cb, ep.ipv4(), ep.port(), NULL);
            }
            return NSP_STATUS_FATAL;
        }

        nsp_status_t obudp::sendto(const void *origin, int cb, const endpoint &ep, const nis_serializer_fp serializer)
        {
            if (INVALID_HUDPLINK != lnk_ && cb > 0 && origin && serializer) {
                return ::udp_write(lnk_, origin, cb, ep.ipv4(), ep.port(), serializer);
            }
            return NSP_STATUS_FATAL;
        }

        const endpoint &obudp::local() const
        {
            return local_;
        }

        void obudp::on_recvdata(const std::basic_string<unsigned char> &data, const endpoint &r_ep)
        {
            ;
        }

        void obudp::on_recvdata(const unsigned char *data, const int cb, const char *ipaddr, const port_t port)
        {
            if (INVALID_HUDPLINK != lnk_ && data && cb > 0 && ipaddr && port > 0) {
                on_recvdata(std::basic_string<unsigned char>(data, cb), endpoint(ipaddr, port));
            }
        }

        void obudp::on_pre_close(void *context)
        {
            ;
        }

        void obudp::on_closed(HUDPLINK previous)
        {
            ;
        }

        void obudp::on_closed()
        {
            HUDPLINK previous = lnk_.exchange(INVALID_HUDPLINK);
            if (INVALID_HUDPLINK != previous) {
                nsp::toolkit::singleton<swnet>::instance()->udp_detach(previous);
                on_closed(previous);
            }
        }

        void obudp::setlnk(const HUDPLINK lnk)
        {
            lnk_ = lnk;
        }

    } // namespace tcpip
} // namespace nsp
