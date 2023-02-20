#pragma once 

#include "singleton.hpp"
#include "args.h"
#include "session.h"

#include <memory>
#include <atomic>

class dispatcher {
    std::atomic<uint64_t> rx_ = { 0 };
    std::atomic<uint64_t> tx_ = { 0 };
    args argument_;
    std::shared_ptr<demo_session> client_ = nullptr;
    unsigned char *package_ = nullptr;
    int pkgsize_ = 0;
    dispatcher();
    friend class nsp::toolkit::singleton<dispatcher>;
    nsp_status_t start_tcp_client();
    nsp_status_t start_udp_client();
    nsp_status_t start_client();

public:
    ~dispatcher();
    nsp_status_t start(int argc, char **argv);
    void on_tcp_recvdata(const std::basic_string<unsigned char> &data);
};

