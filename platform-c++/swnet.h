#if !defined SWNET_INTERFACE_CALL
#define SWNET_INTERFACE_CALL

#include <memory>
#include <unordered_map>
#include <mutex>

#include "nis.h"

#include "singleton.hpp"
#include "network_handler.h"
#include "os_util.hpp"

namespace nsp {
    namespace tcpip {

        class swnet {
            // dl
            // void *shared_library_ = nullptr;

            // TCP
            mutable std::recursive_mutex lock_tcp_redirection_;
            std::unordered_map<HTCPLINK, std::shared_ptr<obtcp>> tcp_object_;

            nsp_status_t tcp_search(const HTCPLINK lnk, std::shared_ptr<obtcp> &object) const;
            void tcp_refobj(const HTCPLINK lnk, const std::function<void( const std::shared_ptr<obtcp>)> &todo);

            // UDP
            mutable std::recursive_mutex lock_udp_redirection_;
            std::unordered_map<HUDPLINK, std::shared_ptr<obudp>> udp_object_;

            nsp_status_t udp_search(const HUDPLINK lnk, std::shared_ptr<obudp> &object) const;
            void udp_refobj(const HUDPLINK lnk, const std::function<void( const std::shared_ptr<obudp>)> &todo);

            // c-d
            friend class nsp::toolkit::singleton<swnet>;
            swnet();
            ~swnet();

            //io
            static void STDCALL tcp_io(const nis_event_t *pParam1, const void *pParam2);
            static void STDCALL udp_io(const nis_event_t *pParam1, const void *pParam2);
            static void STDCALL ecr(const char *host_event, const char *reserved, int rescb);

        public:
            // TCP
            nsp_status_t tcp_create(const std::shared_ptr<obtcp> &object, const char *ipstr, const port_t port);
            nsp_status_t tcp_attach(HTCPLINK lnk, const std::shared_ptr<obtcp> &object);
            void tcp_detach(HTCPLINK lnk);

            // UDP
            nsp_status_t udp_create(const std::shared_ptr<obudp> &object, const char* ipstr, const port_t port, int flag = UDP_FLAG_NONE);
            void udp_detach(HUDPLINK lnk);
        };
    }
}

#endif
