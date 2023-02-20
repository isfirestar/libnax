#include "dispatch.h"

#include <chrono>
#include <thread>
#include <iostream>

dispatcher::dispatcher()
{
    ;
}

dispatcher::~dispatcher()
{
    if (client_) {
        client_->close();
    }

    if (package_ && pkgsize_ > 0) {
        delete []package_;
        package_ = nullptr;
    }
}

nsp_status_t dispatcher::start_tcp_client()
{
    nsp_status_t status;
    try {
        client_ = std::make_shared<demo_session>();

        std::string chost;
        argument_.get_client_host(chost);
        status = client_->create(chost.c_str());
        if (!NSP_SUCCESS(status)) {
            throw status;
        }

        nsp::tcpip::endpoint target;
        status = argument_.get_specify_host(target);
        if (!NSP_SUCCESS(status)) {
            std::string host;
            argument_.get_server_host(host);
            status = client_->connect(host.c_str());
        } else {
            status = client_->connect(target);
        }

        if (!NSP_SUCCESS(status)) {
            throw status;
        }

    } catch (nsp_status_t status) {
        return status;
    } catch (...) {
        return NSP_STATUS_FATAL;
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t dispatcher::start_udp_client()
{
    return NSP_STATUS_FATAL;
}

nsp_status_t dispatcher::start_client()
{
    auto proto = argument_.get_trans_proto();

    nsp_status_t status;
    if (demo_trans_proto::DT_IPPROTO_TCP == proto) {
        status = start_tcp_client();
    } else if (demo_trans_proto::DT_IPPROTO_UDP == proto) {

    } else {
        return posix__makeerror(EPROTO);
    }

    if (!NSP_SUCCESS(status)) {
        return status;
    }

    auto mode = argument_.get_interact_mode();

    // 非echo模式，分配固定的包内存
    if (demo_interact_mode::CS_MODE_ECHO != mode) {
        pkgsize_ = argument_.get_package_size();
        if (pkgsize_ <= 0 ) {
            return posix__makeerror(EINVAL);
        }

        package_ = new (std::nothrow) unsigned char[pkgsize_];
        if (!package_) {
            return posix__makeerror(ENOMEM);
        }
        memset(package_, 0, pkgsize_);

        // 交首包
        client_->send(package_, pkgsize_);
        tx_ += pkgsize_;

        // 开始进行统计
        int elapse = 0;
        std::cout << "Tx\t\t" << "Rx" << std::endl;
        while (1) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            elapse++;
            argument_.display_statistic((double)tx_ / elapse);
            std::cout << "\t\t";
            argument_.display_statistic((double)rx_ / elapse);
            std::cout << std::endl;
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

void dispatcher::on_tcp_recvdata(const std::basic_string<unsigned char> &data)
{
    // 非 echo 模式，继续下一个数据交换
    if (demo_interact_mode::CS_MODE_ECHO != argument_.get_interact_mode()) {
        rx_ += data.size();
        client_->send(package_, pkgsize_);
        tx_ += pkgsize_;
    }
}

nsp_status_t dispatcher::start(int argc, char **argv)
{
    nsp_status_t status = argument_.load_startup_parameters(argc, argv);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    auto sess_type = argument_.get_sess_type();
    switch (sess_type) {
        case demo_sess_type::SESS_TYPE_CLIENT:
            status = start_client();
            break;
        default:
            return posix__makeerror(EINVAL);
    }

    return NSP_STATUS_SUCCESSFUL;
}
