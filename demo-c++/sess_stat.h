#if !defined SESS_STAT_H
#define SESS_STAT_H

#include <atomic>
#include <cstdint>

class sess_stat {
    std::atomic<uint64_t> io_counts_ {0} ;
    std::atomic<uint64_t> sub_io_counts_{0};
    std::atomic<uint64_t> sub_rx_ {0};
    std::atomic<uint64_t> sub_tx_ {0};

    std::atomic<float> max_rtt_ {0.0};

	uint64_t tick = 0; // time tick for check point

public:
	sess_stat();
	~sess_stat();

	void increase_tx(uint64_t inc);
	void increase_rx(uint64_t inc);
    void update_rtt(uint64_t tx_timestamp);
	void print();
};

#endif // !SESS_STAT_H
