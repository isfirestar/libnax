#include <exception>

#include "swnet.h"
#include "os_util.hpp"

#include "logger.h"

namespace nsp {
    namespace tcpip {

        void STDCALL swnet::tcp_io(const nis_event_t *tcp_evt, const void *data) {
            if (!tcp_evt) {
                return;
            }

            switch (tcp_evt->Event) {
                case EVT_RECEIVEDATA:
                    toolkit::singleton<swnet>::instance()->tcp_refobj(tcp_evt->Ln.Tcp.Link, [&] (const std::shared_ptr<obtcp> &object) {
                        object->on_recvdata(((tcp_data_t *) data)->e.Packet.Data, ((tcp_data_t *) data)->e.Packet.Size);
                    });
                    break;
                case EVT_TCP_ACCEPTED:
                    toolkit::singleton<swnet>::instance()->tcp_refobj(tcp_evt->Ln.Tcp.Link, [&] (const std::shared_ptr<obtcp> &object) {
                        object->on_accepted(tcp_evt->Ln.Tcp.Link, ((tcp_data_t *) data)->e.Accept.AcceptLink);
                    });
                    break;
                case EVT_PRE_CLOSE:
                    toolkit::singleton<swnet>::instance()->tcp_refobj(tcp_evt->Ln.Tcp.Link, [&](const std::shared_ptr<obtcp> &object) {
                        object->on_pre_close(((tcp_data_t *) data)->e.PreClose.Context);
                    });
                    break;

                case EVT_TCP_CONNECTED:
                    toolkit::singleton<swnet>::instance()->tcp_refobj(tcp_evt->Ln.Tcp.Link, [&](const std::shared_ptr<obtcp> &object) {
                        object->on_connected2();
                    });
                    break;
                case EVT_CLOSED:
                    toolkit::singleton<swnet>::instance()->tcp_refobj(tcp_evt->Ln.Tcp.Link, [&] (const std::shared_ptr<obtcp> &object) {
                        object->on_closed();
                    });
                    break;
                default:
                    break;
            }
        }

        void STDCALL swnet::udp_io(const nis_event_t *udp_evt, const void *data) {
            if (!udp_evt) {
                return;
            }

            switch (udp_evt->Event) {
                case EVT_RECEIVEDATA:
                    toolkit::singleton<swnet>::instance()->udp_refobj(udp_evt->Ln.Udp.Link, [&] (const std::shared_ptr<obudp> &object) {
                        object->on_recvdata(
                                ((udp_data_t *) data)->e.Packet.Data,
                                ((udp_data_t *) data)->e.Packet.Size,
                                ((udp_data_t *) data)->e.Packet.RemoteAddress,
                                ((udp_data_t *) data)->e.Packet.RemotePort);
                    });
                    break;
                case EVT_PRE_CLOSE:
                    toolkit::singleton<swnet>::instance()->udp_refobj(udp_evt->Ln.Udp.Link, [&](const std::shared_ptr<obudp> &object) {
                        object->on_pre_close(((udp_data_t *) data)->e.PreClose.Context);
                    });
                    break;
                case EVT_CLOSED:
                    toolkit::singleton<swnet>::instance()->udp_refobj(udp_evt->Ln.Udp.Link, [&] (const std::shared_ptr<obudp> &object) {
                        object->on_closed();
                    });
                    break;
                default:
                    break;
            }
        }

        void STDCALL swnet::ecr(const char *host_event, const char *reserved, int rescb) {
            if (host_event) {
                log_save("nshost", kLogLevel_Trace, kLogTarget_Filesystem, "%s", host_event);
            }
        }

        swnet::swnet() {
            nis_checr(&swnet::ecr);
        }

        swnet::~swnet() {
            ;
        }

        int swnet::tcp_create(const std::shared_ptr<obtcp> &object, const char *ipstr, const port_t port) {
            auto lnk = ::tcp_create(&swnet::tcp_io, ipstr, port);
            if (INVALID_HTCPLINK == lnk) {
                return -1;
            }

            object->setlnk(lnk);
            return tcp_attach(lnk, object);
        }

        int swnet::tcp_attach(HTCPLINK lnk, const std::shared_ptr<obtcp> &object) {
            std::lock_guard < decltype(lock_tcp_redirection_) > guard(lock_tcp_redirection_);
            // std::pair<std::unorderd_map<HTCPLINK, std::shared_ptr<obtcp>>::iterator, bool>
            auto insr = tcp_object_.insert(std::pair<HTCPLINK, std::shared_ptr<obtcp>>(lnk,object));
            if (insr.second) {
                return 0;
            }
            return -1;
        }

        void swnet::tcp_detach(HTCPLINK lnk) {
            std::lock_guard < decltype(lock_tcp_redirection_) > guard(lock_tcp_redirection_);
            auto iter = tcp_object_.find(lnk);
            if (tcp_object_.end() != iter) {
                tcp_object_.erase(iter);
            }
        }

        int swnet::tcp_search(const HTCPLINK lnk, std::shared_ptr<obtcp> &object) const {
            std::lock_guard < decltype(lock_tcp_redirection_) > guard(lock_tcp_redirection_);
            auto iter = tcp_object_.find(lnk);
            if (tcp_object_.end() != iter) {
                object = iter->second;
                return 0;
            }
            return -1;
        }

        void swnet::tcp_refobj(const HTCPLINK lnk, const std::function<void( const std::shared_ptr<obtcp>)> &todo) {
            if (INVALID_HTCPLINK != lnk && todo) {
                std::shared_ptr<obtcp> object;
                if (tcp_search(lnk, object) >= 0) {
                    todo(object);
                }
            }
        }
        ///////////////////////////////////////////////////////////   UDP /////////////////////////////////////////////////////////////

        int swnet::udp_create(const std::shared_ptr<obudp> &object, const char* ipstr, const port_t port, int flag) {
            auto lnk = ::udp_create(&swnet::udp_io, ipstr, port, flag);
            if (INVALID_HUDPLINK == lnk) {
                return -1;
            }

            object->setlnk(lnk);

            std::lock_guard < decltype(this->lock_udp_redirection_) > guard(lock_udp_redirection_);
            // std::pair<std::unorderd_map<HUDPLINK, std::shared_ptr<obudp>>::iterator, bool>
            auto insr = udp_object_.insert(std::pair<HUDPLINK, std::shared_ptr<obudp>>(lnk,object));
            if (insr.second) {
                return 0;
            }
            return -1;
        }

        void swnet::udp_detach(HUDPLINK lnk) {
            std::lock_guard < decltype(lock_udp_redirection_) > guard(lock_udp_redirection_);
            auto iter = udp_object_.find(lnk);
            if (udp_object_.end() != iter) {
                udp_object_.erase(iter);
            }
        }

        int swnet::udp_search(const HUDPLINK lnk, std::shared_ptr<obudp> &object) const {
            std::lock_guard < decltype(lock_udp_redirection_) > guard(lock_udp_redirection_);
            auto iter = udp_object_.find(lnk);
            if (udp_object_.end() != iter) {
                object = iter->second;
                return 0;
            }
            return -1;
        }

        void swnet::udp_refobj(const HUDPLINK lnk, const std::function<void( const std::shared_ptr<obudp>)> &todo) {
            if (INVALID_HUDPLINK != lnk && todo) {
                std::shared_ptr<obudp> object;
                if (udp_search(lnk, object) >= 0) {
                    todo(object);
                }
            }
        }

    }
}
