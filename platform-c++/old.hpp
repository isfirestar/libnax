#if !defined NSP_PROTODEF_HEADER_20160614
#define NSP_PROTODEF_HEADER_20160614

#include "toolkit.h"
#include "nisdef.h"

namespace nsp {
    namespace proto {

        namespace sw {

#define PACKET_FLAG_DATA        (0xFDFD) //数据传输
#define PACKET_FLAG_CONTROL        (0xFEFE)  //控制传输
#define PACKET_ENCRYPT_KEY        (0x7F)

#define TcpIsPacketMarkAsControlStream(head)   (PACKET_FLAG_CONTROL == (head)->wFlagHead)
#define TcpIsPacketMarkAsDataStream(head)    (PACKET_FLAG_DATA == (head)->wFlagHead)

            typedef int BOOL;

            typedef struct _TCP_STREAM_HEADER {
                uint16_t wFlagHead;
                uint16_t wPacketFlag;
                uint32_t ulPacketLenght;
                uint32_t ulSrcLen;
            } TCP_STREAM_HEADER, *PTCP_STREAM_HEADER;

            struct protocol {

                static
                int Length() {
                    return sizeof ( TCP_STREAM_HEADER);
                }

                static
                void convert(PTCP_STREAM_HEADER pOriHead, PTCP_STREAM_HEADER pOutputHead, BOOL bNtoH, BOOL bHtoN) {
                    if ((bNtoH && bHtoN) || (!bNtoH && !bHtoN) || !pOriHead) {
                        return;
                    }

                    // 覆盖传入指针的数据区
                    if (!pOutputHead) {
                        pOutputHead = pOriHead;
                    }

                    if (bNtoH) {
                        pOutputHead->wFlagHead = nsp::toolkit::ntohs(pOriHead->wFlagHead);
                        pOutputHead->wPacketFlag = nsp::toolkit::ntohs(pOriHead->wPacketFlag);
                        pOutputHead->ulPacketLenght = nsp::toolkit::ntohl(pOriHead->ulPacketLenght);
                        pOutputHead->ulSrcLen = nsp::toolkit::ntohl(pOriHead->ulSrcLen);
                    }

                    if (bHtoN) {
                        pOutputHead->wFlagHead = nsp::toolkit::htons(pOriHead->wFlagHead);
                        pOutputHead->wPacketFlag = nsp::toolkit::htons(pOriHead->wPacketFlag);
                        pOutputHead->ulPacketLenght = nsp::toolkit::htonl(pOriHead->ulPacketLenght);
                        pOutputHead->ulSrcLen = nsp::toolkit::htonl(pOriHead->ulSrcLen);
                    }
                }

                static
                int STDCALL parser(void *dat, int cb/*传入数据长度*/, int *pkt_cb/*返回包中记录的不含包头的长度, 也就是build阶段的指定长度*/) {
                    if (!dat) return -1;

                    TCP_STREAM_HEADER head;
                    convert((PTCP_STREAM_HEADER) dat, &head, 0, 1);

                    if (!TcpIsPacketMarkAsDataStream(&head)) {
                        return -1;
                    }

                    if (head.ulPacketLenght + sizeof ( TCP_STREAM_HEADER) >= /*TCP_MAXIMUM_PACKET_SIZE*/(50 << 20)) {
                        return -1;
                    }

                    *pkt_cb = head.ulPacketLenght;
                    return 0;
                }

                static
                int STDCALL builder(void *dat, int cb) {
                    PTCP_STREAM_HEADER TcpStreamHead = (PTCP_STREAM_HEADER) dat;
                    TcpStreamHead->wFlagHead = PACKET_FLAG_DATA;
                    TcpStreamHead->wPacketFlag = 0;
                    TcpStreamHead->ulPacketLenght = cb;
                    TcpStreamHead->ulSrcLen = TcpStreamHead->ulPacketLenght;
                    convert(TcpStreamHead, TcpStreamHead, 1, 0);
                    return 0;
                }
            };

        } // sw

        namespace ieee104 {

            typedef struct _HEAD {
                char op_;
                unsigned char cb_;
            } head_t;

#define MAXIMUM_IEEE104_PKTCB  (253)
#define IEEE104_OPCODE    (0x68)

            struct protocol {

                static
                int Length() {
                    return sizeof ( head_t);
                }

                static
                int STDCALL parser(void *dat, int cb/*传入数据长度*/, int *pkt_cb/*返回包中记录的不含包头的长度, 也就是build阶段的指定长度*/) {
                    if (!dat) return -1;

                    head_t *head = (head_t *) dat;
                    if (head->op_ != IEEE104_OPCODE) return -1;

                    *pkt_cb = head->cb_;
                    return 0;
                }

                static
                int STDCALL builder(void *dat, int cb) {
                    if (cb > MAXIMUM_IEEE104_PKTCB) return -1;

                    head_t *head = (head_t *) dat;
                    head->op_ = IEEE104_OPCODE;
                    head->cb_ = cb;
                    return 0;
                }
            };
        } // IEEE104

        namespace nspdef {

            // #define  NSPDEF_OPCODE           ((uint32_t)'dpsN')
            static const unsigned char NSPDEF_OPCODE[4] = {'N', 's', 'p', 'd'};

            typedef struct _HEAD {
                uint32_t op_;
                uint32_t cb_;
            } head_t;

            struct protocol {

                static
                int Length() {
                    return sizeof ( head_t);
                }

                static
                int STDCALL parser(void *dat, int cb, int *pkt_cb) {
                    if (!dat) {
                        return -1;
                    }

                    head_t *head = (head_t *) dat;
                    if (0 != memcmp(NSPDEF_OPCODE, &head->op_, sizeof (NSPDEF_OPCODE))) {
                        return -1;
                    }
                    *pkt_cb = head->cb_;
                    return 0;
                }

                static
                int STDCALL builder(void *dat, int cb) {
                    if (!dat || cb <= 0) {
                        return -1;
                    }
                    head_t *head = (head_t *) dat;
                    memcpy(&head->op_, NSPDEF_OPCODE, sizeof (NSPDEF_OPCODE));
                    head->cb_ = cb;
                    return 0;
                }
            };
        } // nspdef

        namespace nspnull {

            struct protocol {

                static
                int Length() {
                    return 0;
                }

                static
                int STDCALL parser(void *dat, int cb, int *pkt_cb) {
                    return -1;
                }

                static
                int STDCALL builder(void *dat, int cb) {
                    return 0;
                }
            };
        } // nspnull

    } // // namespace proto
} // namespace nsp

#endif
