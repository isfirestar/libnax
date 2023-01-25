#include "sess_stat.h"

#include "os_util.hpp"
#include "toolkit.h"

#include <stdio.h>

sess_stat::sess_stat()
{
	tick = nsp::os::clock_gettime();
}

sess_stat::~sess_stat()
{
	tick = nsp::os::clock_gettime();
}

void sess_stat::increase_tx( uint64_t inc )
{
	sub_tx_ += inc;
}

void sess_stat::increase_rx( uint64_t inc )
{
	sub_rx_ += inc;
	++io_counts_;
	++sub_io_counts_;
}

void sess_stat::update_rtt(uint64_t tx_timestamp)
{
    uint64_t rtt;
    double rtt_milliseconds;

    if (0 == tx_timestamp){
        return;
    }

    rtt = nsp::os::clock_monotonic() - tx_timestamp;
    rtt_milliseconds = (double)rtt / 10000;

    if ( 0 == max_rtt_ || max_rtt_ < rtt_milliseconds) {
        max_rtt_ = rtt_milliseconds;
    }
}

void sess_stat::print()
{
    uint64_t current_tick = nsp::os::clock_gettime();
	if ( current_tick <= tick ) {
		return;
	}
    uint64_t escaped_tick = current_tick - tick;

    double escaped_seconds = (double) ( (double)escaped_tick / 10000000);
    tick = current_tick;

    uint64_t io_counts = io_counts_;

    // accumulate IO count
    uint32_t iops = (uint32_t) ((double)sub_io_counts_ / escaped_seconds);

    // calculate Tx speed in delta
    char tx_speed[128];
    uint64_t tx_bps_u =  sub_tx_;
    tx_bps_u *= 8;
    double tx_bps = (double) tx_bps_u / escaped_seconds;
    posix__sprintf(tx_speed, cchof(tx_speed), "%.2f bps", tx_bps);
    if (tx_bps / 1024 > 1) {
		tx_bps /= 1024;
        posix__sprintf(tx_speed, cchof(tx_speed), "%.2f Kbps", tx_bps);
    }
    if (tx_bps / 1024 > 1) {
        tx_bps /= 1024;
        posix__sprintf(tx_speed, cchof(tx_speed), "%.2f Mbps", tx_bps);
    }

    // calculate Rx speed in delta
    char rx_speed[128];
    uint64_t rx_bps_u = sub_rx_;
    rx_bps_u *= 8;
    double rx_bps = (double) rx_bps_u / escaped_seconds;
    posix__sprintf(rx_speed, cchof(rx_speed), "%.2f bps", rx_bps);
    if (rx_bps / 1024 > 1) {
		rx_bps /= 1024;
        posix__sprintf(rx_speed, cchof(rx_speed), "%.2f Kbps",rx_bps );
    }
    if (rx_bps / 1024 > 1) {
		rx_bps /= 1024;
        posix__sprintf(rx_speed, cchof(rx_speed), "%.2f Mbps", rx_bps);
    }

    float rtt = max_rtt_;
    printf("\t" UINT64_STRFMT" \t%.2f\t%u\t\t%s\t%s\n", io_counts, rtt, iops, tx_speed, rx_speed);

    // clear cumulative data after print
    sub_io_counts_ = 0;
    sub_rx_ = 0;
    sub_tx_ = 0;
    max_rtt_ = 0.0;
}

