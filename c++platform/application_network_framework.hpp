#if !defined APPLICATION_NETWORK_FRAMEWORK_H
#define APPLICATION_NETWORK_FRAMEWORK_H

#include <memory>
#include <map>
#include <vector>
#include <mutex>

#include "network_handler.h"
#include "serialize.hpp"
#include "log.h"
#include "old.hpp"

/*
 *  The application uses the class defined by the header file as the base class to define its own network session
 *  and instantiate it.
 *
 *  notes that whether UDP or TCP client have these attributes:
 *  1. instantiation must use @shared_ptr object, initial by using @std::make_shared implement
 *  2. framwork will keep one reference count of object after @create framwork method success, it means
 *      object can not automatic deconstruction by zeroing reference count in only user layer, unless @close framwork method called
 *  3. It is strong recommended to use the @proto_interface framework to serialize or build network packets.
 *      and than post  the packets by calling framework method @psend, it is simpler and more reliable
 *  4. receive data by overwrite framework virtual method @on_recvdata(const std::string &) associated TCP protocol or
 *      @on_recvdata(const std::string &, const endpoint &) associated UDP protocol
 *  5. during receive proce, the thread layout of Linux is very difference from MS-WINDOWNS,
 *      applications do not need to pay special attention to the way these threads are laid out,
 *      however, it is recommended to switch threads to complete the long time-consuming operations, such as disk-IO or any wait method
 *
 *  Terminology:
 *
 *  Session:
 *      A stateful interaction between a Client and a Server. Some Sessions last only as long as the Network
 *       Connection, others can span multiple consecutive Network Connections between a Client and a Server
 */

namespace nsp {
    namespace tcpip {
        inline int STDCALL packet_serialize(unsigned char *dest, const void *origin, int cb)
        {
            const proto::proto_interface *package = (const proto::proto_interface *)origin;
            return (NULL != package->serialize(dest)) ? 0 : -1;
        }
    };
};

namespace nsp {
    namespace tcpip {

        template<class T>
        class tcp_application_service : public obtcp
        {
            tcp_application_service(HTCPLINK lnk) = delete;

            // finalized the virtual function
            // because inherit class does no need to known how the link created in server.
            virtual void on_accepted(HTCPLINK lnk) override final
            {
                std::shared_ptr<T> sptr = std::make_shared<T>(lnk);
                std::weak_ptr<obtcp> wptr = sptr->attach();
                if (wptr.expired()) {
                    return;
                }

                try {
                    sptr->bind_object(shared_from_this());

                    // the server can declined the establish request when initial connected
                    if (sptr->on_established() < 0){
                        throw -ENETRESET;
                    }
                } catch (...) {
                    sptr->close();
                    return;
                }

                std::lock_guard < decltype(client_locker_) > guard(client_locker_);
                auto iter = client_set_.find(lnk);
                if (client_set_.end() == iter) {
                    client_set_[lnk] = wptr;
                }
            }

            // as the listening socket, there will be no actual data packets.
            virtual void on_recvdata(const std::basic_string<unsigned char> &data) override final
            {
                abort();
            }

            std::map<HTCPLINK, std::weak_ptr<obtcp>> client_set_;
            mutable std::recursive_mutex client_locker_;
        public:
            tcp_application_service() : obtcp()
            {
                ;
            }

            virtual ~tcp_application_service()
            {
                close();
            }

            // begin the services
            int begin(const endpoint &ep)
            {
                return ( (create(ep) >= 0) ? listen() : -1);
            }

            int begin(const std::string &epstr)
            {
                endpoint ep;
                if (endpoint::build(epstr, ep) < 0) {
                    return -1;
                }
                return begin(ep);
            }

            // the notification from client closed event
            void on_client_closed(const HTCPLINK lnk)
            {
                std::lock_guard < decltype(client_locker_) > guard(client_locker_);
                auto iter = client_set_.find(lnk);
                if (client_set_.end() != iter) {
                    client_set_.erase(iter);
                }
            }

            int notify_one(HTCPLINK lnk, const std::function<int( const std::shared_ptr<T> &client)> &todo)
            {
                std::shared_ptr<T> client;
                {
                    std::lock_guard < decltype(client_locker_) > guard(client_locker_);
                    auto iter = client_set_.find(lnk);
                    if (client_set_.end() == iter) {
                        return -1;
                    }

                    if (iter->second.expired()) {
                        return -1;
                    }

                    auto obptr = iter->second.lock();
                    if (!obptr) {
                        client_set_.erase(iter);
                        return -1;
                    }

                    client = std::static_pointer_cast< T >(obptr);
                }

                if (todo) {
                    return todo(client);
                }
                return -1;
            }

            void notify_all(const std::function<void( const std::shared_ptr<T> &client)> &todo)
            {
                std::vector<std::weak_ptr < obtcp>> duplicated;
                {
                    std::lock_guard < decltype(client_locker_) > guard(client_locker_);
                    auto iter = client_set_.begin();
                    while (client_set_.end() != iter) {
                        if (iter->second.expired()) {
                            iter = client_set_.erase(iter);
                        } else {
                            duplicated.push_back(iter->second);
                            ++iter;
                        }
                    }
                }

                for (auto &iter : duplicated) {
                    auto obptr = iter.lock();
                    std::shared_ptr<T> client = std::static_pointer_cast< T >(obptr);
                    if (client && todo) {
                        todo(client);
                    }
                }

            }

            // search a client object by it's HTCPLINK flag
            int search_client_by_link(const HTCPLINK lnk, std::shared_ptr<T> &client) const
            {
                std::lock_guard < decltype(client_locker_) > guard(client_locker_);
                auto iter = client_set_.find(lnk);
                if (client_set_.end() != iter) {
                    client = std::static_pointer_cast< T >(iter->second.lock());
                    return 0;
                } else {
                    return -1;
                }
            }
        };
    }
}

namespace nsp {
    namespace tcpip {

        template<class T>
        class tcp_application_client : public obtcp
        {
            std::weak_ptr<tcp_application_service<tcp_application_client<T>>> tcp_application_server_;
        public:
            tcp_application_client() : obtcp()
            {
                settst<T>();
            }

            tcp_application_client(HTCPLINK lnk) : obtcp(lnk)
            {
                settst<T>();
            }

            virtual ~tcp_application_client()
            {
				;
            }

            // if using @proto::proto_interface mode to serialize or deserialize,
            // calling thread can send packets direct by @proto::proto_interface object
            // this operation is recommended by framework
            int psend(const proto::proto_interface *package)
            {
                if (!package) {
                    return -1;
                }

                return obtcp::send(package, package->length(), &nsp::tcpip::packet_serialize);
            }

            virtual void bind_object(const std::shared_ptr<obtcp> &object) override final
            {
                tcp_application_server_ = std::static_pointer_cast< tcp_application_service<tcp_application_client < T>> >(object);
            }

            // overwrite this virtual method to handle the event of inital connection created
            // return negative integer value to cancel the enstablished link
            virtual int on_established()
            {
                return 0;
            }
        protected:
            // finalize the virtual method @on_closed, instead by @on_disconnected
            // if the server object is still existed, notify it to remove weak pointer of this object.
            virtual void on_closed(HTCPLINK previous) override final
            {
                auto sptr = tcp_application_server_.lock();
                if (sptr) {
                    std::shared_ptr<tcp_application_service<tcp_application_client < T>>> srvptr =
                            std::static_pointer_cast< tcp_application_service<tcp_application_client < T>> >(sptr);
                    srvptr->on_client_closed(previous);
                }

                on_disconnected(previous);
            }

			// syn request in cleint session? this maybe a fatal error.
            virtual void on_accepted(HTCPLINK lnk) override final
            {
                abort();
            }

            virtual void on_disconnected(const HTCPLINK previous)
            {
				;
            }
        };
        typedef tcp_application_client<nsp::proto::nspdef::protocol> nsp_application_client;

        class udp_application_client : public obudp
        {
        public:
            udp_application_client() : obudp()
            {
                ;
            }

            virtual ~udp_application_client()
             {
                ;
            }

            // like tcp session, support @psend method to send packet which using proto_interface serializer
            int psend(const proto::proto_interface *package, const endpoint &ep)
            {
                if (!package) {
                    return -1;
                }

                return obudp::sendto(package, package->length(), ep, &nsp::tcpip::packet_serialize);
            }
        };

        typedef udp_application_client udp_application_server;

    } // namespace tcpip
} // namespace nsp

#endif
